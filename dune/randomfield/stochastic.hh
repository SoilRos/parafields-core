// -*- tab-width: 2; indent-tabs-mode: nil -*-
#ifndef DUNE_RANDOMFIELD_STOCHASTIC_HH
#define	DUNE_RANDOMFIELD_STOCHASTIC_HH

#include<fstream>

#include<dune/common/power.hh>
#include<dune/common/timer.hh>
//#include<dune/pdelab/gridfunctionspace/lfsindexcache.hh>

#include<dune/randomfield/fieldtraits.hh>

namespace Dune {
  namespace RandomField {

      /*
       * @brief Part of random field that consists of cell values
       */
    template<typename T>
      class StochasticPart
      {

        public:

          typedef T Traits;

        private:

          typedef typename Traits::RF RF;

          enum{dim = Traits::dimDomain};

          friend class RandomFieldMatrix<Traits>;

          Dune::shared_ptr<Traits> traits;

          int rank, commSize;
          std::vector<RF>           extensions;
          std::vector<unsigned int> cells;
          unsigned int              level;
          std::vector<unsigned int> localCells;
          std::vector<unsigned int> localOffset;
          unsigned int              localDomainSize;
          unsigned int              sliceSize;
          std::vector<unsigned int> localEvalCells;
          std::vector<unsigned int> localEvalOffset;
          int                       procPerDim;

          std::vector<RF> dataVector;
          mutable std::vector<RF> evalVector;
          mutable std::vector<std::vector<RF> > overlap;

          mutable bool evalValid;
          mutable std::vector<unsigned int> cellIndices;
          mutable std::vector<unsigned int> evalIndices;
          mutable std::vector<unsigned int> countIndices;

        public:

          /**
           * @brief Constructor
           */
          StochasticPart(const Dune::shared_ptr<Traits>& traits_, const std::string& fieldName, const std::string& fileName)
            : traits(traits_), cellIndices((*traits).dim), evalIndices((*traits).dim), countIndices((*traits).dim)
          {
            update();

            if (fileName != "")
            {
              if(!fileExists(fileName+"."+fieldName+".stoch.h5"))
                DUNE_THROW(Dune::Exception,"File is missing: " + fileName + "." + fieldName + ".stoch.h5");

              Dune::Timer readTimer(false);
              readTimer.start();

              if (rank == 0) std::cout << "loading random field from file " << fileName + "." + fieldName << std::endl;
              readParallelFromHDF5<RF,dim>(dataVector, localCells, localOffset, MPI_COMM_WORLD, "/"+fieldName, fileName+"."+fieldName+".stoch.h5");

              readTimer.stop();
              if (rank == 0) std::cout << "Time for loading random field from file " << fileName + "." + fieldName << ": " << readTimer.elapsed() << std::endl;

              evalValid  = false;
            }
            else
            {
              if (rank == 0) std::cout << "generating homogeneous random field" << std::endl;

              zero();
            }
          }

          /**
           * @brief Calculate container sizes after construction or refinement
           */
          void update()
          {
            rank            = (*traits).rank;
            commSize        = (*traits).commSize;
            extensions      = (*traits).extensions;
            cells           = (*traits).cells;
            level           = (*traits).level;
            localCells      = (*traits).localCells;
            localOffset     = (*traits).localOffset;
            localDomainSize = (*traits).localDomainSize;

            procPerDim = 1;
            while (Dune::Power<dim>::eval(procPerDim) != commSize)
            {
              if (Dune::Power<dim>::eval(procPerDim) > commSize)
                DUNE_THROW(Dune::Exception,"number of processors not square (resp. cubic)");
              procPerDim++;
            }
            localEvalCells.resize(dim);
            localEvalOffset.resize(dim);
            for (unsigned int i = 0; i < dim; i++)
              localEvalCells[i] = cells[i] / procPerDim;
            if (dim == 3)
            {
              localEvalOffset[0] = (rank%(procPerDim*procPerDim))%procPerDim * localEvalCells[0];
              localEvalOffset[1] = (rank%(procPerDim*procPerDim))/procPerDim * localEvalCells[1];
              localEvalOffset[2] =  rank/(procPerDim*procPerDim)             * localEvalCells[2];
            }
            else
            {
              localEvalOffset[0] = rank%procPerDim * localEvalCells[0];
              localEvalOffset[1] = rank/procPerDim * localEvalCells[1];
            }

            dataVector.resize(localDomainSize);
            evalVector.resize(localDomainSize);
            overlap.resize((*traits).dim*2);
            for (unsigned int i = 0; i < dim; i++)
            {
              overlap[2*i    ].resize(localDomainSize/localEvalCells[i]);
              overlap[2*i + 1].resize(localDomainSize/localEvalCells[i]);
            }

            cellIndices.resize((*traits).dim);
            evalIndices.resize((*traits).dim);
            countIndices.resize((*traits).dim);

            evalValid = false;
          }

          /**
           * @brief Write stochastic part of field to hard disk
           */
          void writeToFile(const std::string& fileName, const std::string& fieldName) const
          {
            Dune::Timer writeTimer(false);
            writeTimer.start();

            if (rank == 0) std::cout << "writing random field to file " << fileName+"."+fieldName << std::endl;
            writeParallelToHDF5<RF,dim>((*traits).cells, dataVector, localCells, localOffset, MPI_COMM_WORLD, "/"+fieldName, fileName+"."+fieldName+".stoch.h5");

            if (rank == 0)
            {
              std::ofstream file(fileName+"."+fieldName+".xdmf",std::ofstream::trunc);

              file << "<?xml version=\"1.0\" ?>"                                                            << std::endl;
              file << "<!DOCTYPE Xdmf SYSTEM \"Xdmf.dtd\" []>"                                              << std::endl;
              file << "<Xdmf Version=\"2.0\">"                                                              << std::endl;
              file << " <Domain>"                                                                           << std::endl;
              file << "  <Grid Name=\"StructuredGrid\" GridType=\"Uniform\">"                               << std::endl;
              file << "   <Topology TopologyType=\"3DRectMesh\" NumberOfElements=\"";
              for (unsigned int i = 0; i < dim; i++)
                file << cells[dim-(i+1)] << " ";
              file << "\"/>"                                                                                << std::endl;
              file << "   <Geometry GeometryType=\"origin_dxdydz\">" << std::endl;
              file << "    <DataItem Dimensions=\"3\" NumberType=\"Float\" Precision=\"4\" Format=\"XML\">" << std::endl;
              file << "     0. 0. 0."                                                                       << std::endl;
              file << "    </DataItem>"                                                                     << std::endl;
              file << "    <DataItem Dimensions=\"3\" NumberType=\"Float\" Precision=\"4\" Format=\"XML\">" << std::endl;
              file << "     ";
              file << cells[0]/extensions[0] << " "; // additional entry to visualize 2D files
              file << cells[dim-2]/extensions[dim-2] << " ";
              file << cells[dim-1]/extensions[dim-1] << std::endl;
              file << "    </DataItem>"                                                                     << std::endl;
              file << "   </Geometry>"                                                                      << std::endl;
              file << "   <Attribute Name=\"";
              file << fieldName;
              file << "\" AttributeType=\"Scalar\" Center=\"Cell\">"                                        << std::endl;
              file << "    <DataItem Dimensions=\"";
              for (unsigned int i = 0; i < dim; i++)
                file << cells[dim-(i+1)] << " ";
              file << "\" NumberType=\"Float\" Precision=\"4\" Format=\"HDF\">"                             << std::endl;
              file << "     " << fileName+"."+fieldName+".stoch.h5" << ":/" << fieldName                    << std::endl;
              file << "    </DataItem>"                                                                     << std::endl;
              file << "   </Attribute>"                                                                     << std::endl;
              file << "  </Grid>"                                                                           << std::endl;
              file << " </Domain>"                                                                          << std::endl;
              file << "</Xdmf>"                                                                             << std::endl;
            }

            writeTimer.stop();
            if (rank == 0) std::cout << "Time for writing random field to file " << fileName + "." + fieldName << ": " << writeTimer.elapsed() << std::endl;
          }

          /**
           * @brief Addition assignment operator
           */
          StochasticPart& operator+=(const StochasticPart& other)
          {
            for (unsigned int i = 0; i < localDomainSize; ++i)
            {
              dataVector[i] += other.dataVector[i];
            }

            evalValid = false;

            return *this;
          }

          /**
           * @brief Subtraction assignment operator
           */
          StochasticPart& operator-=(const StochasticPart& other)
          {
            for (unsigned int i = 0; i < localDomainSize; ++i)
            {
              dataVector[i] -= other.dataVector[i];
            }

            evalValid = false;

            return *this;
          }

          /**
           * @brief Multiplication with scalar
           */
          StochasticPart& operator*=(const RF alpha)
          {
            for (unsigned int i = 0; i < localDomainSize; ++i)
            {
              dataVector[i] *= alpha;
            }

            evalValid = false;

            return *this;
          }

          /**
           * @brief AXPY scaled addition
           */
          StochasticPart& axpy(const StochasticPart& other, const RF alpha)
          {
            for (unsigned int i = 0; i < localDomainSize; ++i)
            {
              dataVector[i] += other.dataVector[i] * alpha;
            }

            evalValid = false;

            return *this;
          }

          /**
           * @brief Scalar product
           */
          RF operator*(const StochasticPart& other) const
          {
            RF sum = 0., mySum = 0.;

            for (unsigned int i = 0; i < localDomainSize; ++i)
            {
              mySum += dataVector[i] * other.dataVector[i];
            }

            MPI_Allreduce(&mySum,&sum,1,MPI_DOUBLE,MPI_SUM,MPI_COMM_WORLD);
            return sum;
          }

          /**
           * @brief Evaluate stochastic part at given location
           */
          void evaluate(const typename Traits::DomainType& location, typename Traits::RangeType& output) const
          {
            if (!evalValid)
              dataToEval();

            (*traits).coordsToIndices(location,evalIndices,localEvalOffset);

            for (unsigned int i = 0; i < dim; i++)
            {
              if (evalIndices[i] > localEvalCells[i])
                countIndices[i] = 2*i;
              else if (evalIndices[i] == localEvalCells[i])
                countIndices[i] = 2*i+1;
              else
                countIndices[i] = 2*dim;
            }

            if (dim == 3)
            {
              if (countIndices[0] == 2*dim && countIndices[1] == 2*dim && countIndices[2] != 2*dim)
              {
                output[0] = overlap[countIndices[2]][evalIndices[0] + evalIndices[1]*localEvalCells[0]];
              }
              else if (countIndices[0] == 2*dim && countIndices[1] != 2*dim && countIndices[2] == 2*dim)
              {
                output[0] = overlap[countIndices[1]][evalIndices[2] + evalIndices[0]*localEvalCells[2]];
              }
              else if (countIndices[0] != 2*dim && countIndices[1] == 2*dim && countIndices[2] == 2*dim)
              {
                output[0] = overlap[countIndices[0]][evalIndices[1] + evalIndices[2]*localEvalCells[1]];
              }
              else
              {
                for (unsigned int i = 0; i < dim; i++)
                {
                  if (evalIndices[i] > localEvalCells[i])
                    evalIndices[i]++;
                  else if (evalIndices[i] == localEvalCells[i])
                    evalIndices[i]--;
                }

                const unsigned int index = (*traits).indicesToIndex(evalIndices,localEvalCells);
                output[0] = evalVector[index];
              }
            }
            else
            {
              if (countIndices[0] == 2*dim && countIndices[1] != 2*dim)
              {
                output[0] = overlap[countIndices[1]][evalIndices[0]];
              }
              else if (countIndices[0] != 2*dim && countIndices[1] == 2*dim)
              {
                output[0] = overlap[countIndices[0]][evalIndices[1]];
              }
              else
              {
                for (unsigned int i = 0; i < dim; i++)
                {
                  if (evalIndices[i] > localEvalCells[i])
                    evalIndices[i]++;
                  else if (evalIndices[i] == localEvalCells[i])
                    evalIndices[i]--;
                }

                const unsigned int index = (*traits).indicesToIndex(evalIndices,localEvalCells);
                output[0] = evalVector[index];
              }
            }
          }

          /**
           * @brief Set stochastic part to zero
           */
          void zero()
          {
            for (unsigned int i = 0; i < localDomainSize; i++)
            {
              dataVector[i] = 0.;
            }

            evalValid = false;
          }

          /**
           * @brief Double spatial resolution and transfer field values
           */
          void refine()
          {
            Dune::Timer timer(false);
            timer.start();

            if (level != (*traits).level)
            {
              const std::vector<RF> oldData = dataVector;
              update();

              std::vector<unsigned int> oldLocalCells(dim);
              for (unsigned int i = 0; i < dim; i++)
              {
                oldLocalCells[i] = localCells[i]/2;
              }

              dataVector.resize(localDomainSize);

              std::vector<unsigned int> oldIndices(dim);
              std::vector<unsigned int> newIndices(dim);
              if (dim == 3)
              {
                for (oldIndices[2] = 0; oldIndices[2] < oldLocalCells[2]; oldIndices[2]++)
                  for (oldIndices[1] = 0; oldIndices[1] < oldLocalCells[1]; oldIndices[1]++)
                    for (oldIndices[0] = 0; oldIndices[0] < oldLocalCells[0]; oldIndices[0]++)
                    {
                      newIndices[0] = 2*oldIndices[0];
                      newIndices[1] = 2*oldIndices[1];
                      newIndices[2] = 2*oldIndices[2];

                      const unsigned int oldIndex = (*traits).indicesToIndex(oldIndices,oldLocalCells);
                      const unsigned int newIndex = (*traits).indicesToIndex(newIndices,localCells);
                      const RF oldValue = oldData[oldIndex];

                      dataVector[newIndex                                                  ] = oldValue;
                      dataVector[newIndex + 1                                              ] = oldValue;
                      dataVector[newIndex + localCells[0]                                  ] = oldValue;
                      dataVector[newIndex + localCells[0] + 1                              ] = oldValue;
                      dataVector[newIndex + localCells[1]*localCells[0]                    ] = oldValue;
                      dataVector[newIndex + localCells[1]*localCells[0] + 1                ] = oldValue;
                      dataVector[newIndex + localCells[1]*localCells[0] + localCells[0]    ] = oldValue;
                      dataVector[newIndex + localCells[1]*localCells[0] + localCells[0] + 1] = oldValue;
                    }
              }
              else
              {
                for (oldIndices[1] = 0; oldIndices[1] < oldLocalCells[1]; oldIndices[1]++)
                  for (oldIndices[0] = 0; oldIndices[0] < oldLocalCells[0]; oldIndices[0]++)
                  {
                    newIndices[0] = 2*oldIndices[0];
                    newIndices[1] = 2*oldIndices[1];

                    const unsigned int oldIndex = (*traits).indicesToIndex(oldIndices,oldLocalCells);
                    const unsigned int newIndex = (*traits).indicesToIndex(newIndices,localCells);
                    const RF oldValue = oldData[oldIndex];

                    dataVector[newIndex                    ] = oldValue;
                    dataVector[newIndex + 1                ] = oldValue;
                    dataVector[newIndex + localCells[0]    ] = oldValue;
                    dataVector[newIndex + localCells[0] + 1] = oldValue;
                  }
              }

              evalValid = false;

              timer.stop();
              if (rank == 0) std::cout << "Time for StochasticPart refine " << timer.lastElapsed() << std::endl;
            }
          }

          void localize(const typename Traits::DomainType& center, const RF radius)
          {
            typename Traits::DomainType location;
            const RF factor = std::pow(2.*3.14159,-(dim/2.));
            RF distSquared;

            for (unsigned int i = 0; i < localDomainSize; i++)
            {
              (*traits).indexToIndices(i,cellIndices,localCells);
              (*traits).indicesToCoords(cellIndices,localOffset,location);

              distSquared = 0.;
              for (unsigned int j = 0; j < dim; j++)
                distSquared += (location[j] - center[j]) * (location[j] - center[j]);

              dataVector[i] *= factor * std::exp(-0.5*distSquared/(radius*radius));
            }

            evalValid = false;
          }

        private:

          /**
           * @brief Convert data in striped (FFT compatible) format to setup using blocks
           */
          void dataToEval() const
          {
            Dune::Timer timer(false);
            timer.start();

            std::vector<RF> resorted(dataVector.size(),0.);
            std::vector<RF> temp = dataVector;

            if (commSize == 1)
            {
              evalVector = dataVector;
              evalValid  = true;
              return;
            }

            MPI_Request request;
            MPI_Status status;

            unsigned int numSlices = procPerDim*localDomainSize/localCells[0];
            unsigned int sliceSize = localDomainSize/numSlices;

            if (dim == 3)
            {
              unsigned int px = procPerDim;
              unsigned int py = procPerDim;
              unsigned int ny = localCells[dim-2];
              unsigned int nz = localCells[dim-1];
              unsigned int dy = ny/py;

              for (unsigned int i = 0; i < numSlices; i++)
              {
                unsigned int term1 = (i%px) * (dy*nz);
                unsigned int term2 = ((i/(dy*px)*dy)%ny) * (nz*px);
                unsigned int term3 = (i/(ny*px)) * dy;
                unsigned int term4 = (i/px) % dy;

                unsigned int iNew = term1 + term2 + term3 + term4;

                for (unsigned int j = 0; j < sliceSize; j++)
                {
                  resorted[iNew * sliceSize + j] = dataVector[i * sliceSize + j];
                }
              }
            }
            else
            {
              for (unsigned int i = 0; i < numSlices; i++)
              {
                unsigned int iNew = i/procPerDim + (i%procPerDim)*localCells[dim-1];
                for (unsigned int j = 0; j < sliceSize; j++)
                {
                  resorted[iNew * sliceSize + j] = dataVector[i * sliceSize + j];
                }
              }
            }

            unsigned int numComms;
            if (dim == 3)
              numComms = procPerDim*procPerDim;
            else
              numComms = procPerDim;

            for (unsigned int i = 0; i < numComms; i++)
            {
              MPI_Isend(&(resorted  [i*localDomainSize/numComms]), localDomainSize/numComms, MPI_DOUBLE, (rank/numComms)*numComms + i, 0, MPI_COMM_WORLD, &request);
            }

            for (unsigned int i = 0; i < numComms; i++)
            {
              MPI_Recv (&(evalVector[i*localDomainSize/numComms]), localDomainSize/numComms, MPI_DOUBLE, (rank/numComms)*numComms + i, 0, MPI_COMM_WORLD, &status);
            }

            MPI_Barrier(MPI_COMM_WORLD);

            exchangeOverlap();

            evalValid = true;

            timer.stop();
            if (rank == 0) std::cout << "Time for StochasticPart dataToEval " << timer.lastElapsed() << std::endl;
          }

          /**
           * @brief Convert data in blocks to setup using stripes (FFT compatible)
           */
          void evalToData()
          {
            Dune::Timer timer(false);
            timer.start();

            if (commSize == 1)
            {
              dataVector = evalVector;
              return;
            }

            std::vector<RF> resorted(dataVector.size(),0.);

            MPI_Request request;
            MPI_Status status;

            unsigned int numComms;
            if (dim == 3)
              numComms = procPerDim*procPerDim;
            else
              numComms = procPerDim;

            for (unsigned int i = 0; i < numComms; i++)
            {
              MPI_Isend(&(evalVector[i*localDomainSize/numComms]), localDomainSize/numComms, MPI_DOUBLE, (rank/numComms)*numComms + i, 0, MPI_COMM_WORLD, &request);
            }

            for (unsigned int i = 0; i < numComms; i++)
            {
              MPI_Recv (&(resorted  [i*localDomainSize/numComms]), localDomainSize/numComms, MPI_DOUBLE, (rank/numComms)*numComms + i, 0, MPI_COMM_WORLD, &status);
            }

            unsigned int numSlices = procPerDim*localDomainSize/localCells[0];
            unsigned int sliceSize = localDomainSize/numSlices;

            if (dim == 3)
            {
              for (unsigned int i = 0; i < numSlices; i++)
              {
                unsigned int px = procPerDim;
                unsigned int py = procPerDim;
                unsigned int ny = localCells[dim-2];
                unsigned int nz = localCells[dim-1];
                unsigned int dy = ny/py;

                unsigned int term1 = (i%px) * (dy*nz);
                unsigned int term2 = ((i/(dy*px)*dy)%ny) * (nz*px);
                unsigned int term3 = (i/(ny*px)) * dy;
                unsigned int term4 = (i/px) % dy;

                unsigned int iNew = term1 + term2 + term3 + term4;

                for (unsigned int j = 0; j < sliceSize; j++)
                {
                  dataVector[i * sliceSize + j] = resorted[iNew * sliceSize + j];
                }
              }
            }
            else
            {
              for (unsigned int i = 0; i < numSlices; i++)
              {
                unsigned int iNew = i/procPerDim + (i%procPerDim)*localCells[dim-1];
                for (unsigned int j = 0; j < sliceSize; j++)
                {
                  dataVector[i * sliceSize + j] = resorted[iNew * sliceSize + j];
                }
              }
            }

            MPI_Barrier(MPI_COMM_WORLD);

            timer.stop();
            if (rank == 0) std::cout << "Time for StochasticPart evalToData " << timer.lastElapsed() << std::endl;
          }

          /**
           * @brief Communicate the overlap regions at the block boundaries
           */
          void exchangeOverlap() const
          {
            std::vector<unsigned int> neighbor(2*dim);
            std::vector<std::vector<RF> > extract = overlap;

            if (dim == 3)
            {
              for (unsigned int i = 0; i < dim; i++)
              {
                const unsigned int iNext     = (i+1)%dim;
                const unsigned int iNextNext = (i+2)%dim;
                for (evalIndices[iNext] = 0; evalIndices[iNext] < localEvalCells[iNext]; evalIndices[iNext]++)
                {
                  for (evalIndices[iNextNext] = 0; evalIndices[iNextNext] < localEvalCells[iNextNext]; evalIndices[iNextNext]++)
                  {
                    evalIndices[i] = 0;
                    const unsigned int index  = (*traits).indicesToIndex(evalIndices,localEvalCells);
                    extract[2*i  ][evalIndices[iNext] + evalIndices[iNextNext] * localEvalCells[iNext]] = evalVector[index];

                    evalIndices[i] = localEvalCells[i] - 1;
                    const unsigned int index2 = (*traits).indicesToIndex(evalIndices,localEvalCells);
                    extract[2*i+1][evalIndices[iNext] + evalIndices[iNextNext] * localEvalCells[iNext]] = evalVector[index2];
                  }
                }
              }

              neighbor[0] = (rank/procPerDim)*procPerDim + (rank    +(procPerDim-1))%procPerDim;
              neighbor[1] = (rank/procPerDim)*procPerDim + (rank    +1             )%procPerDim;
              neighbor[2] = (rank/(procPerDim*procPerDim))*(procPerDim*procPerDim) + (rank+(procPerDim*procPerDim-procPerDim))%(procPerDim*procPerDim);
              neighbor[3] = (rank/(procPerDim*procPerDim))*(procPerDim*procPerDim) + (rank+procPerDim                        )%(procPerDim*procPerDim);
              neighbor[4] = (rank+(commSize-(procPerDim*procPerDim)))%commSize;
              neighbor[5] = (rank+(procPerDim*procPerDim)           )%commSize;
            }
            else
            {
              for (unsigned int i = 0; i < dim; i++)
              {
                const unsigned int iNext = (i+1)%dim;
                for (evalIndices[iNext] = 0; evalIndices[iNext] < localEvalCells[iNext]; evalIndices[iNext]++)
                {
                  evalIndices[i] = 0;
                  const unsigned int index  = (*traits).indicesToIndex(evalIndices,localEvalCells);
                  extract[2*i  ][evalIndices[iNext]] = evalVector[index];

                  evalIndices[i] = localEvalCells[i] - 1;
                  const unsigned int index2 = (*traits).indicesToIndex(evalIndices,localEvalCells);
                  extract[2*i+1][evalIndices[iNext]] = evalVector[index2];
                }
              }

              neighbor[0] = (rank/procPerDim)*procPerDim + (rank+(procPerDim-1))%     procPerDim;
              neighbor[1] = (rank/procPerDim)*procPerDim + (rank+1             )%     procPerDim;
              neighbor[2] = (rank+(commSize-procPerDim))%commSize;
              neighbor[3] = (rank+procPerDim           )%commSize;
            }

            MPI_Request request;
            MPI_Status status;

            for (unsigned int i = 0; i < dim; i++)
            {
              MPI_Isend(&(extract[2*i  ][0]), localDomainSize/localEvalCells[i], MPI_DOUBLE, neighbor[2*i  ], 0, MPI_COMM_WORLD, &request);
              MPI_Recv (&(overlap[2*i+1][0]), localDomainSize/localEvalCells[i], MPI_DOUBLE, neighbor[2*i+1], 0, MPI_COMM_WORLD, &status);

              MPI_Isend(&(extract[2*i+1][0]), localDomainSize/localEvalCells[i], MPI_DOUBLE, neighbor[2*i+1], 0, MPI_COMM_WORLD, &request);
              MPI_Recv (&(overlap[2*i  ][0]), localDomainSize/localEvalCells[i], MPI_DOUBLE, neighbor[2*i  ], 0, MPI_COMM_WORLD, &status);
            }

            MPI_Barrier(MPI_COMM_WORLD);
          }

      };

  }
}

#endif // DUNE_RANDOMFIELD_STOCHASTIC_HH

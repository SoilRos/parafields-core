// -*- tab-width: 2; indent-tabs-mode: nil -*-
#ifndef DUNE_RANDOMFIELD_FIELDTRAITS_HH
#define	DUNE_RANDOMFIELD_FIELDTRAITS_HH

#include<array>
#include<vector>

#include <fftw3.h>
#include <fftw3-mpi.h>

#include<dune/common/parametertreeparser.hh>

namespace Dune {
  namespace RandomField {

    /**
     * @brief Default load balance strategy, taken from dune-grid to avoid hard dependency
     */
    template<long unsigned int dim>
      class DefaultLoadBalance
      {
        public:

          /** @brief Distribute a structured grid across a set of processors
           *
           * @param [in] size Number of elements in each coordinate direction, for the entire grid
           * @param [in] P    Number of processors
           */
          void loadbalance (const std::array<int, dim>& size, int P, std::array<int,dim>& dims) const
          {
            double opt = 1e100;
            std::array<int,dim> trydims;

            optimize_dims(dim-1,size,P,dims,trydims,opt);
          }

        private:

          void optimize_dims (
              int i,
              const std::array<int,dim>& size,
              int P, std::array<int,dim>& dims,
              std::array<int,dim>& trydims,
              double& opt
              ) const
          {
            if (i > 0) // test all subdivisions recursively
            {
              for (unsigned int k = 1; k <= (unsigned int)P; k++)
                if (P%k == 0)
                {
                  // P divisible by k
                  trydims[i] = k;
                  optimize_dims(i-1,size,P/k,dims,trydims,opt);
                }
            }
            else
            {
              // found a possible combination
              trydims[0] = P;

              // check for optimality
              double m = -1.;

              for (unsigned int k = 0; k < dim; k++)
              {
                double mm = ((double)size[k])/((double)trydims[k]);
                if (fmod((double)size[k],(double)trydims[k]) > 0.0001)
                  mm *= 3;
                if ( mm > m )
                  m = mm;
              }
              if (m < opt)
              {
                opt = m;
                dims = trydims;
              }
            }
          }
      };

    template<typename Traits> class TrendPart;
    template<typename Traits> class TrendComponent;
    template<typename Traits> class ImageComponent;
    template<typename Traits> class StochasticPart;
    template<typename GridTraits, template<typename> class IsoMatrix,
      template<typename> class AnisoMatrix> class RandomField;

    /**
     * @brief Traits for the RandomField class
     */
    template<typename GridTraits, template<typename> class IsoMatrix, template<typename> class AnisoMatrix>
      class RandomFieldTraits
      {
        using ThisType = RandomFieldTraits<GridTraits,IsoMatrix,AnisoMatrix>;

        public:

        enum {dim = GridTraits::dim};

        using RF          = typename GridTraits::RangeField;
        using DomainField = typename GridTraits::DomainField;
        using DomainType  = typename GridTraits::Domain;
        using RangeType   = typename GridTraits::Scalar;

        using IsoMatrixType    = IsoMatrix<ThisType>;
        using AnisoMatrixType  = AnisoMatrix<ThisType>;

#if HAVE_DUNE_PDELAB
        // allows treating a RandomField as a PDELab function
        typedef typename Dune::YaspGrid<dim>::LeafGridView GridViewType;
        enum {dimRange  = 1};
        enum {dimDomain = dim};
#endif // HAVE_DUNE_PDELAB

        private:

        friend TrendPart<ThisType>;
        friend TrendComponent<ThisType>;
        friend ImageComponent<ThisType>;
        friend StochasticPart<ThisType>;
        friend IsoMatrix<ThisType>;
        friend AnisoMatrix<ThisType>;
        friend RandomField<GridTraits,IsoMatrix,AnisoMatrix>;

        // MPI constants
        int rank, commSize;

        std::array<int,dim> procPerDim;

        const Dune::ParameterTree& config;
        const MPI_Comm             comm;

        const std::array<RF,dim> extensions;
        unsigned int             level;
        std::array<RF,dim>       meshsize;
        RF                       cellVolume;

        const RF           variance;
        const std::string  covariance;
        const bool         periodic;
        const bool         approximate;
        const bool         verbose;
        const unsigned int cgIterations;
        const bool         cacheInvMatvec;
        const bool         cacheInvRootMatvec;

        ptrdiff_t allocLocal, localN0, local0Start;

        // factor used in domain embedding
        unsigned int embeddingFactor;

        // properties of random field
        std::array<unsigned int,dim> cells;
        unsigned int                 domainSize;
        std::array<unsigned int,dim> localCells;
        std::array<unsigned int,dim> localOffset;
        unsigned int                 localDomainSize;

        // properties on extended domain
        std::array<unsigned int,dim> extendedCells;
        unsigned int                 extendedDomainSize;
        std::array<unsigned int,dim> localExtendedCells;
        std::array<unsigned int,dim> localExtendedOffset;
        unsigned int                 localExtendedDomainSize;

        mutable std::array<unsigned int,dim> globalIndices;
        mutable std::array<unsigned int,dim> localIndices;

        public:

        template<typename LoadBalance>
          RandomFieldTraits(
              const Dune::ParameterTree& config_,
              const LoadBalance& loadBalance,
              const MPI_Comm comm_
              )
          : config(config_), comm(comm_),
          extensions        (config.get<std::array<RF,dim>>          ("grid.extensions")),
          variance          (config.get<RF>                          ("stochastic.variance")),
          covariance        (config.get<std::string>                 ("stochastic.covariance")),
          periodic          (config.get<bool>                        ("randomField.periodic",false)),
          approximate       (config.get<bool>                        ("randomField.approximate",false)),
          verbose           (config.get<bool>                        ("randomField.verbose",false)),
          cgIterations      (config.get<unsigned int>                ("randomField.cgIterations",100)),
          cacheInvMatvec    (config.get<bool>                        ("randomField.cacheInvMatvec",true)),
          cacheInvRootMatvec(config.get<bool>                        ("randomField.cacheInvRootMatvec",false)),
          embeddingFactor   (config.get<unsigned int>                ("randomField.embeddingFactor",2)),
          cells             (config.get<std::array<unsigned int,dim>>("grid.cells"))
        {
          MPI_Comm_rank(comm,&rank);
          MPI_Comm_size(comm,&commSize);

          // dune-grid load balancers want int as data type
          std::array<int,dim> intCells;
          for (unsigned int i = 0; i < dim; i++)
            intCells[i] = cells[i];
          loadBalance.loadbalance(intCells,commSize,procPerDim);

          level = 0;

          if (periodic && embeddingFactor != 1)
          {
            if (verbose && rank == 0)
              std::cout << "periodic boundary conditions are synonymous with embeddingFactor == 1,"
                << " enforcing consistency" << std::endl;
            embeddingFactor = 1;
          }

          fftw_mpi_init();
          update();
        }

        /**
         * @brief Compute constants after construction or refinement
         */
        void update()
        {
          // ensures that FFTW can divide data equally between processes
          if (cells[dim-1] % commSize != 0)
            DUNE_THROW(Dune::Exception,"number of cells in last dimension has to be multiple of numProc");
          if (dim == 1 && cells[0] % (commSize*commSize) != 0)
            DUNE_THROW(Dune::Exception,"in 1D, number of cells has to be multiple of numProc^2");

          for (unsigned int i = 0; i < dim; i++)
          {
            meshsize[i]      = extensions[i] / cells[i];
            extendedCells[i] = embeddingFactor*cells[i];
          }

          getFFTData(allocLocal, localN0, local0Start);

          for (unsigned int i = 0; i < dim - 1; i++)
          {
            localExtendedCells [i] = extendedCells[i];
            localExtendedOffset[i] = 0;
            localCells [i] = cells[i];
            localOffset[i] = 0;
          }
          localExtendedCells [dim-1] = localN0;
          localExtendedOffset[dim-1] = local0Start;
          localCells [dim-1] = localN0/embeddingFactor;
          localOffset[dim-1] = local0Start/embeddingFactor;

          domainSize              = 1;
          extendedDomainSize      = 1;
          localDomainSize         = 1;
          localExtendedDomainSize = 1;
          cellVolume              = 1.;
          for (unsigned int i = 0; i < dim; i++)
          {
            domainSize              *= cells[i];
            extendedDomainSize      *= extendedCells[i];
            localDomainSize         *= localCells[i];
            localExtendedDomainSize *= localExtendedCells[i];
            cellVolume              *= meshsize[i];
          }

          if (verbose && rank == 0)
          {
            std::cout << "RandomField size:        " << localDomainSize << std::endl;
            std::cout << "RandomField cells:       ";
            for (unsigned int i = 0; i < dim; i++)
            {
              std::cout << cells[i] << " ";
            }
            std::cout << std::endl;
            std::cout << "RandomField local cells: ";
            for (unsigned int i = 0; i < dim; i++)
            {
              std::cout << localCells[i] << " ";
            }
            std::cout << std::endl;
            std::cout << "RandomField cell volume: " << cellVolume << std::endl;
          }
        }

        /**
         * @brief Request global refinement of the data structure
         */
        void refine()
        {
          for (unsigned int i = 0; i < dim; ++i)
            cells[i] *= 2;

          level++;

          update();
        }

        /**
         * @brief Request global coarsening of the data structure
         */
        void coarsen()
        {
          for (unsigned int i = 0; i < dim; ++i)
          {
            if (cells[i] % 2 != 0)
              DUNE_THROW(Dune::Exception, "cannot coarsen odd number of cells");
            cells[i] /= 2;
          }

          level--;

          update();
        }

        /**
         * @brief Get the domain decomposition data of the Fourier transform
         */
        template<typename T>
          void getFFTData(T& allocLocal, T& localN0, T& local0Start) const
          {
            ptrdiff_t n[dim];

            for (unsigned int i = 0; i < dim; i++)
              n[i] = extendedCells[dim-1-i];

            if (dim == 1)
            {
              ptrdiff_t localN02, local0Start2;
              allocLocal = fftw_mpi_local_size_1d(n[0], comm, FFTW_FORWARD, FFTW_ESTIMATE,
                  &localN0, &local0Start, &localN02, &local0Start2);
              if (localN0 != localN02 || local0Start != local0Start2)
                DUNE_THROW(Dune::Exception,"1d size / offset results don't match");
            }
            else
              allocLocal = fftw_mpi_local_size(dim, n, comm, &localN0, &local0Start);
          }

        /**
         * @brief Convert an index tuple into a one dimensional encoding
         */
        template<unsigned int currentDim = 0>
        static unsigned int indicesToIndex(
            const std::array<unsigned int,dim>& indices,
            const std::array<unsigned int,dim>& bound
            )
        {
          if constexpr(currentDim == dim-1)
            return indices[currentDim];
          else
            return indices[currentDim]
              + bound[currentDim] * indicesToIndex<currentDim+1>(indices,bound);
        }

        /**
         * @brief Convert a one dimensional encoding into the original index tuple
         */
        template<unsigned int currentDim = 0>
        static void indexToIndices(
            const unsigned int index,
            std::array<unsigned int,dim>& indices,
            const std::array<unsigned int,dim>& bound
            )
        {
          indices[currentDim] = index % bound[currentDim];
          if constexpr(currentDim != dim-1)
            indexToIndices<currentDim+1>(index/bound[currentDim],indices,bound);
        }

        /**
         * @brief Convert spatial coordinates into the corresponding integer indices
         */
        void coordsToIndices(
            const DomainType& location,
            std::array<unsigned int,dim>& localIndices,
            const std::array<unsigned int,dim>& offset
            ) const
        {
          for (unsigned int i = 0; i < dim; i++)
          {
            globalIndices[i] = (unsigned int) (location[i] * (cells[i] + 1e-6) / extensions[i]);
            localIndices[i]  = globalIndices[i] - offset[i];
          }
        }

        /**
         * @brief Convert integer indices into corresponding spatial coordinates
         */
        void indicesToCoords(
            const std::array<unsigned int,dim>& localIndices,
            const std::array<unsigned int,dim>& offset,
            DomainType& location
            ) const
        {
          for (unsigned int i = 0; i < dim; i++)
          {
            globalIndices[i] = localIndices[i] + offset[i];
            location[i]      = (globalIndices[i] + 0.5) * extensions[i] / cells[i];
          }
        }

      };

  }
}

#endif // DUNE_RANDOMFIELD_FIELDTRAITS_HH

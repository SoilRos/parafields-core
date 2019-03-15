// -*- tab-width: 2; indent-tabs-mode: nil -*-
#ifndef DUNE_RANDOMFIELD_RANDOMFIELD_HH
#define	DUNE_RANDOMFIELD_RANDOMFIELD_HH

#include<dune/common/parametertree.hh>

#if HAVE_DUNE_FUNCTIONS
// for VTK output functionality
#include<dune/grid/yaspgrid.hh>
#include<dune/grid/io/file/vtk.hh>
#include<dune/functions/gridfunctions/analyticgridviewfunction.hh>
#endif // HAVE_DUNE_FUNCTIONS

#include<dune/randomfield/io.hh>
#include<dune/randomfield/fieldtraits.hh>
#include<dune/randomfield/trend.hh>
#include<dune/randomfield/stochastic.hh>
#include<dune/randomfield/matrix.hh>
#include<dune/randomfield/mutators.hh>
#include<dune/randomfield/legacyvtk.hh>

namespace Dune {
  namespace RandomField {

    /**
     * @brief Gaussian random field in 1D, 2D or 3D
     */
    template<typename GridTraits, template<typename> class Matrix = RandomFieldMatrix>
      class RandomField
      {
        protected:
          
          using Traits         = RandomFieldTraits<GridTraits,Matrix>;
          using StochasticPart = StochasticPart<Traits,Matrix>;
          using RF             = typename Traits::RF;

          // to allow reading in constructor
          class ParamTreeHelper
          {
            Dune::ParameterTree config;

            public:

            ParamTreeHelper(const std::string& fileName = "")
            {
              if (fileName != "")
              {
                Dune::ParameterTreeParser parser;
                parser.readINITree(fileName+".field",config);
              }
            }

            const Dune::ParameterTree& get() const
            {
              return config;
            }
          };

          const ParamTreeHelper                   treeHelper;
          const Dune::ParameterTree               config;
          const ValueTransform<RF>                valueTransform;
          std::shared_ptr<Traits>                 traits;
          std::shared_ptr<Matrix<Traits> >        matrix;
          TrendPart<Traits>                       trendPart;
          StochasticPart                          stochasticPart;
          mutable std::shared_ptr<StochasticPart> invMatvecPart;
          mutable bool                            invMatvecValid;
          mutable std::shared_ptr<StochasticPart> invRootMatvecPart;
          mutable bool                            invRootMatvecValid;

        public:

          /**
           * @brief Constructor reading from file or creating homogeneous field
           */
          template<typename LoadBalance = DefaultLoadBalance<GridTraits::dim> >
            explicit RandomField(const Dune::ParameterTree& config_, const std::string& fileName = "", const LoadBalance& loadBalance = LoadBalance(), const MPI_Comm comm = MPI_COMM_WORLD)
            : config(config_), valueTransform(config), traits(new Traits(config,loadBalance,comm)), matrix(new Matrix<Traits>(traits)),
            trendPart(config,traits,fileName), stochasticPart(traits,fileName),
            invMatvecValid(false), invRootMatvecValid(false)
            {
              if ((*traits).cacheInvMatvec)
                invMatvecPart = std::shared_ptr<StochasticPart>(new StochasticPart(stochasticPart));

              if ((*traits).cacheInvRootMatvec)
                invRootMatvecPart = std::shared_ptr<StochasticPart>(new StochasticPart(stochasticPart));
            }

          /**
           * @brief Constructor reading field and config from file
           */
          template<typename LoadBalance = DefaultLoadBalance<GridTraits::dim> >
            RandomField(const std::string& fileName, const LoadBalance& loadBalance = LoadBalance(), const MPI_Comm comm = MPI_COMM_WORLD)
            : treeHelper(fileName), config(treeHelper.get()), valueTransform(config), traits(new Traits(config,loadBalance,comm)), matrix(new Matrix<Traits>(traits)),
            trendPart(config,traits,fileName), stochasticPart(traits,fileName),
            invMatvecValid(false), invRootMatvecValid(false)
            {
              if ((*traits).cacheInvMatvec)
                invMatvecPart = std::shared_ptr<StochasticPart>(new StochasticPart(stochasticPart));

              if ((*traits).cacheInvRootMatvec)
                invRootMatvecPart = std::shared_ptr<StochasticPart>(new StochasticPart(stochasticPart));
            }

          /**
           * @brief Constructor copying traits and covariance matrix
           */
          RandomField(const RandomField& other, const std::string& fileName)
            : config(other.config), valueTransform(other.valueTransform), traits(other.traits), matrix(other.matrix),
            trendPart(config,traits,fileName), stochasticPart(traits,fileName),
            invMatvecValid(false), invRootMatvecValid(false)
        {
          if ((*traits).cacheInvMatvec)
            invMatvecPart = std::shared_ptr<StochasticPart>(new StochasticPart(stochasticPart));

          if ((*traits).cacheInvRootMatvec)
            invRootMatvecPart = std::shared_ptr<StochasticPart>(new StochasticPart(stochasticPart));
        }

#if HAVE_DUNE_PDELAB
           /**
           * @brief Constructor converting from GridFunctionSpace and GridVector
           */
          template<typename GFS, typename Field>
            RandomField(const RandomField& other, const GFS& gfs, const Field& field)
            : config(other.config), valueTransform(other.valueTransform), traits(other.traits), matrix(other.matrix),
            trendPart(other.trendPart,gfs,field), stochasticPart(other.stochasticPart,gfs,field),
            invMatvecValid(false), invRootMatvecValid(false)
        {
          if ((*traits).cacheInvMatvec)
            invMatvecPart = std::shared_ptr<StochasticPart>(new StochasticPart(stochasticPart));

          if ((*traits).cacheInvRootMatvec)
            invRootMatvecPart = std::shared_ptr<StochasticPart>(new StochasticPart(stochasticPart));
        }

          /**
           * @brief Constructor converting from DiscreteGridFunction
           */
          template<typename DGF>
            RandomField(const RandomField& other, const DGF& dgf)
            : config(other.config), valueTransform(other.valueTransform), traits(other.traits), matrix(other.matrix),
            trendPart(other.trendPart,dgf), stochasticPart(other.stochasticPart,dgf),
            invMatvecValid(false), invRootMatvecValid(false)
        {
          if ((*traits).cacheInvMatvec)
            invMatvecPart = std::shared_ptr<StochasticPart>(new StochasticPart(stochasticPart));

          if ((*traits).cacheInvRootMatvec)
            invRootMatvecPart = std::shared_ptr<StochasticPart>(new StochasticPart(stochasticPart));
        }
#endif // HAVE_DUNE_PDELAB

          /**
           * @brief Copy constructor
           */
          RandomField(const RandomField& other)
            : config(other.config), valueTransform(other.valueTransform), traits(other.traits), matrix(other.matrix),
            trendPart(other.trendPart), stochasticPart(other.stochasticPart),
            invMatvecValid(other.invMatvecValid), invRootMatvecValid(other.invRootMatvecValid)
        {
          if ((*traits).cacheInvMatvec && (*(other.traits)).cacheInvMatvec)
            invMatvecPart = std::shared_ptr<StochasticPart>(new StochasticPart(*(other.invMatvecPart)));

          if ((*traits).cacheInvRootMatvec && (*(other.traits)).cacheInvRootMatvec)
            invRootMatvecPart = std::shared_ptr<StochasticPart>(new StochasticPart(*(other.invRootMatvecPart)));
        }

          /**
           * @brief Assignment operator
           */
          RandomField& operator=(const RandomField& other)
          {
            if (this != &other)
            {
              config             = other.config;
              valueTransform     = other.valueTransform;
              traits             = other.traits;
              matrix             = other.matrix;
              trendPart          = other.trendPart;
              stochasticPart     = other.stochasticPart;

              if ((*traits).cacheInvMatvec)
              {
                invMatvecPart = std::shared_ptr<StochasticPart>(new StochasticPart(*(other.invMatvecPart)));
                invMatvecValid = other.invMatvecValid;
              }

              if ((*traits).cacheInvRootMatvec)
              {
                invRootMatvecPart = std::shared_ptr<StochasticPart>(new StochasticPart(*(other.invRootMatvecPart)));
                invRootMatvecValid = other.invRootMatvecValid;
              }
            }

            return *this;
          }

          /**
           * @brief Cell volume of the random field discretization
           */
          RF cellVolume() const
          {
            return (*traits).cellVolume;
          }

          /**
           * @brief Number of degrees of freedom
           */
          unsigned int dofs() const
          {
            return stochasticPart.dofs() + trendPart.dofs();
          }

          /**
           * @brief Generate a field with the desired correlation structure
           */
          void generate(bool allowNonWorldComm = false)
          {
            // create seed out of current time
            unsigned int seed = (unsigned int) clock();
            // different seeds for different fields
            seed += static_cast<int>(reinterpret_cast<uintptr_t>(&stochasticPart));

            generate(seed,allowNonWorldComm);
          }

          /**
           * @brief Generate a field with desired correlation structure using seed
           */
          void generate(unsigned int seed, bool allowNonWorldComm = false)
          {
            if (((*traits).comm != MPI_COMM_WORLD) && !allowNonWorldComm)
              DUNE_THROW(Dune::Exception,
                  "generation of inconsistent fields prevented, set allowNonWorldComm = true if you really want this");

            std::cout << "generate with seed: " << seed << std::endl;

            (*matrix).generateField(seed,stochasticPart);
            trendPart.generate(seed);
          }

          /**
           * @brief Generate a field without correlation structure (i.e. noise)
           */
          void generateUncorrelated(bool allowNonWorldComm = false)
          {
            // create seed out of current time
            unsigned int seed = (unsigned int) clock();
            // different seeds for different fields
            seed += static_cast<int>(reinterpret_cast<uintptr_t>(&stochasticPart));

            generate(seed,allowNonWorldComm);
          }

          /**
           * @brief Generate a field containing noise using seed
           */
          void generateUncorrelated(unsigned int seed, bool allowNonWorldComm = false)
          {
            if (((*traits).comm != MPI_COMM_WORLD) && !allowNonWorldComm)
              DUNE_THROW(Dune::Exception,
                  "generation of inconsistent fields prevented, set allowNonWorldComm = true if you really want this");

            (*matrix).generateUncorrelatedField(stochasticPart);
            trendPart.generateUncorrelated();
          }

#if HAVE_DUNE_GRID
          /**
           * @brief Evaluate the random field in the coordinates of an element
           */
          template<typename Element>
            void evaluate(
                const Element& elem,
                const typename Traits::DomainType& xElem,
                typename Traits::RangeType& output
                ) const
            {
              const typename Traits::DomainType location = elem.geometry().global(xElem);
              evaluate(location,output);
            }
#endif // HAVE_DUNE_GRID

          /**
           * @brief Evaluate the random field at given coordinates
           */
          void evaluate(const typename Traits::DomainType& location, typename Traits::RangeType& output) const
          {
            typename Traits::RangeType stochastic = 0., trend = 0.;

            stochasticPart.evaluate(location,stochastic);
            trendPart     .evaluate(location,trend);

            output = stochastic + trend;
            valueTransform.apply(output);
          }

          /**
           * @brief Export random field to files on disk
           */
          void writeToFile(const std::string& fileName) const
          {
            stochasticPart.writeToFile(fileName);
            trendPart     .writeToFile(fileName);

            std::ofstream file(fileName+".field",std::ofstream::trunc);
            config.report(file);
          }

          /**
           * @brief Export random field as flat unstructured VTK file, requires dune-grid and dune-functions
           */
          template<typename GridView>
            void writeToVTK(const std::string& fileName, const GridView& gv) const
            {
#if HAVE_DUNE_FUNCTIONS
              Dune::VTKWriter<GridView> vtkWriter(gv,Dune::VTK::conforming);
              auto f = Dune::Functions::makeAnalyticGridViewFunction([&](auto x)
                  {typename Traits::RangeType output; this->evaluate(x,output); return output;},gv);
              vtkWriter.addCellData(f,VTK::FieldInfo(fileName,VTK::FieldInfo::Type::scalar,1));
              vtkWriter.pwrite(fileName,"","",Dune::VTK::appendedraw);
#else //HAVE_DUNE_FUNCTIONS
              DUNE_THROW(Dune::NotImplemented,"Unstructured VTK output requires dune-grid and dune-functions");
#endif //HAVE_DUNE_FUNCTIONS
            }

          /**
           * @brief Export random field as unstructured VTK file, requires dune-grid and dune-functions
           */
          template<typename GridView>
            void writeToVTKSeparate(const std::string& fileName, const GridView& gv) const
            {
#if HAVE_DUNE_FUNCTIONS
              Dune::VTKWriter<GridView> vtkWriter(gv,Dune::VTK::conforming);
              {
                auto f = Dune::Functions::makeAnalyticGridViewFunction([&](auto x)
                    {typename Traits::RangeType output; stochasticPart.evaluate(x,output); return output;},gv);
                vtkWriter.addCellData(f,VTK::FieldInfo("stochastic",VTK::FieldInfo::Type::scalar,1));
              }
              for (unsigned int i = 0; i < trendPart.size(); i++)
              {
                const TrendComponent<Traits>& component = trendPart.getComponent(i);
                auto f = Dune::Functions::makeAnalyticGridViewFunction([&](auto x)
                    {typename Traits::RangeType output; component.evaluate(x,output); return output;},gv);
                vtkWriter.addCellData(f,VTK::FieldInfo(component.name(),VTK::FieldInfo::Type::scalar,1));
              }
              vtkWriter.pwrite(fileName,"","",Dune::VTK::appendedraw);
#else //HAVE_DUNE_FUNCTIONS
              DUNE_THROW(Dune::NotImplemented,"Unstructured VTK output requires dune-grid and dune-functions");
#endif //HAVE_DUNE_FUNCTIONS
            }

          /**
           * @brief Export random field as flat Legacy VTK file
           */
          void writeToLegacyVTK(const std::string& fileName) const
          {
            if ((*traits).commSize > 1)
              DUNE_THROW(Dune::NotImplemented,"legacy VTK output doesn't work for parallel runs");

            LegacyVTKWriter<Traits> legacyWriter(config,fileName);
            legacyWriter.writeScalarData("field",*this);
          }

          /**
           * @brief Export random field as separate Legacy VTK entries
           */
          void writeToLegacyVTKSeparate(const std::string& fileName) const
          {
            if ((*traits).commSize > 1)
              DUNE_THROW(Dune::NotImplemented,"legacy VTK output doesn't work for parallel runs");

            LegacyVTKWriter<Traits> legacyWriter(config,fileName);
            legacyWriter.writeScalarData("stochastic",stochasticPart);
            for (unsigned int i = 0; i < trendPart.size(); i++)
            {
              const TrendComponent<Traits>& component = trendPart.getComponent(i);
              legacyWriter.writeScalarData(component.name(),component);
            }
          }

          /**
           * @brief Make random field homogeneous
           */
          void zero()
          {
            trendPart     .zero();
            stochasticPart.zero();

            if ((*traits).cacheInvMatvec)
            {
              (*invMatvecPart).zero();
              invMatvecValid = true;
            }

            if ((*traits).cacheInvRootMatvec)
            {
              (*invRootMatvecPart).zero();
              invRootMatvecValid = true;
            }
          }

          /**
           * @brief Double spatial resolution of covariance matrix
           */
          void refineMatrix()
          {
            (*traits).refine();
            (*matrix).update();
          }

          /**
           * @brief Double spatial resolution of random field
           */
          void refine()
          {
            if ((*traits).cacheInvMatvec && invMatvecValid)
            {
              (*invMatvecPart).refine();
              stochasticPart = (*matrix) * (*invMatvecPart);

              const RF scale = std::pow(0.5,-(*traits).dim);
              stochasticPart *= scale;
              *invMatvecPart *= scale;

              if ((*traits).cacheInvRootMatvec)
              {
                *invRootMatvecPart = (*matrix).multiplyRoot(*invMatvecPart);

                *invRootMatvecPart *= scale;
                invRootMatvecValid = true;
              }

            }
            else if ((*traits).cacheInvRootMatvec && invRootMatvecValid)
            {
              (*invRootMatvecPart).refine();
              stochasticPart = (*matrix).multiplyRoot(*invRootMatvecPart);

              const RF scale = std::pow(0.5,-(*traits).dim);
              stochasticPart     *= scale;
              *invRootMatvecPart *= scale;

              if ((*traits).cacheInvMatvec)
              {
                *invMatvecPart = stochasticPart;
                invMatvecValid = false;
              }
            }
            else
            {
              stochasticPart.refine();

              if ((*traits).cacheInvMatvec)
                (*invMatvecPart).refine();

              if ((*traits).cacheInvRootMatvec)
                (*invRootMatvecPart).refine();
            }
          }

          /**
           * @brief Double spatial resolution of covariance matrix
           */
          void coarsenMatrix()
          {
            (*traits).coarsen();
            (*matrix).update();
          }

          /**
           * @brief Reduce spatial resolution of random field
           */
          void coarsen()
          {
            if ((*traits).cacheInvMatvec && invMatvecValid)
            {
              (*invMatvecPart).coarsen();
              stochasticPart = (*matrix) * (*invMatvecPart);

              const RF scale = std::pow(0.5,(*traits).dim);
              stochasticPart *= scale;
              *invMatvecPart *= scale;

              if ((*traits).cacheInvRootMatvec)
              {
                *invRootMatvecPart = (*matrix).multiplyRoot(*invMatvecPart);

                *invRootMatvecPart *= scale;
                invRootMatvecValid = true;
              }

            }
            else if ((*traits).cacheInvRootMatvec && invRootMatvecValid)
            {
              (*invRootMatvecPart).coarsen();
              stochasticPart = (*matrix).multiplyRoot(*invRootMatvecPart);

              const RF scale = std::pow(0.5,(*traits).dim);
              stochasticPart     *= scale;
              *invRootMatvecPart *= scale;

              if ((*traits).cacheInvMatvec)
              {
                *invMatvecPart = stochasticPart;
                invMatvecValid = false;
              }
            }
            else
            {
              stochasticPart.coarsen();

              if ((*traits).cacheInvMatvec)
                (*invMatvecPart).coarsen();

              if ((*traits).cacheInvRootMatvec)
                (*invRootMatvecPart).coarsen();
            }
          }

          /**
           * @brief Addition assignment operator
           */
          RandomField& operator+=(const RandomField& other)
          {
            trendPart      += other.trendPart;
            stochasticPart += other.stochasticPart;

            if ((*traits).cacheInvMatvec)
            {
              if ((*(other.traits)).cacheInvMatvec)
              {
                *invMatvecPart += *(other.invMatvecPart);
                invMatvecValid = invMatvecValid && other.invMatvecValid;
              }
              else
                invMatvecValid = false;
            }

            if ((*traits).cacheInvRootMatvec)
            {
              if ((*(other.traits)).cacheInvRootMatvec)
              {
                *invRootMatvecPart += *(other.invRootMatvecPart);
                invRootMatvecValid = invRootMatvecValid && other.invRootMatvecValid;
              }
              else
                invRootMatvecValid = false;
            }

            return *this;
          }

          /**
           * @brief Subtraction assignment operator
           */
          RandomField& operator-=(const RandomField& other)
          {
            trendPart      -= other.trendPart;
            stochasticPart -= other.stochasticPart;

            if ((*traits).cacheInvMatvec)
            {
              if ((*(other.traits)).cacheInvMatvec)
              {
                *invMatvecPart -= *(other.invMatvecPart);
                invMatvecValid = invMatvecValid && other.invMatvecValid;
              }
              else
                invMatvecValid = false;
            }

            if ((*traits).cacheInvRootMatvec)
            {
              if ((*(other.traits)).cacheInvRootMatvec)
              {
                *invRootMatvecPart -= *(other.invRootMatvecPart);
                invRootMatvecValid = invRootMatvecValid && other.invRootMatvecValid;
              }
              else
                invRootMatvecValid = false;
            }

            return *this;
          }

          /**
           * @brief Multiplication with scalar
           */
          RandomField& operator*=(const RF alpha)
          {
            trendPart      *= alpha;
            stochasticPart *= alpha;

            if ((*traits).cacheInvMatvec)
              *invMatvecPart *= alpha;

            if ((*traits).cacheInvRootMatvec)
              *invRootMatvecPart *= alpha;

            return *this;
          }

          /**
           * @brief AXPY scaled addition
           */
          RandomField& axpy(const RandomField& other, const RF alpha)
          {
            trendPart     .axpy(other.trendPart     ,alpha);
            stochasticPart.axpy(other.stochasticPart,alpha);

            if ((*traits).cacheInvMatvec)
            {
              if ((*(other.traits)).cacheInvMatvec)
              {
                (*invMatvecPart).axpy(*(other.invMatvecPart),alpha);
                invMatvecValid = invMatvecValid && other.invMatvecValid;
              }
              else
                invMatvecValid = false;
            }

            if ((*traits).cacheInvRootMatvec)
            {
              if ((*(other.traits)).cacheInvRootMatvec)
              {
                (*invRootMatvecPart).axpy(*(other.invRootPart),alpha);
                invRootMatvecValid = invRootMatvecValid && other.invRootMatvecValid;
              }
              else
                invRootMatvecValid = false;
            }

            return *this;
          }

          /**
           * @brief AXPY scaled addition (swapped arguments)
           */
          RandomField& axpy(const RF alpha, const RandomField& other)
          {
            return axpy(other,alpha);
          }

          /**
           * @brief Scalar product
           */
          RF operator*(const RandomField& other) const
          {
            RF output = 0.;

            output += (*this).stochasticPart * other.stochasticPart;
            output += (*this).trendPart * other.trendPart;

            return output;
          }

          /**
           * @brief Multiply random field with covariance matrix
           */
          void timesMatrix()
          {
            if ((*traits).cacheInvMatvec)
            {
              *invMatvecPart = stochasticPart;
              invMatvecValid  = true;
            }

            if ((*traits).cacheInvRootMatvec)
            {
              *invRootMatvecPart = (*matrix).multiplyRoot(stochasticPart);
              invRootMatvecValid = true;
            }

            stochasticPart = (*matrix) * stochasticPart;

            trendPart.timesMatrix();
          }

          /**
           * @brief Multiply random field with inverse of covariance matrix
           */
          void timesInverseMatrix()
          {
            if ((*traits).cacheInvMatvec && invMatvecValid)
            {
              if ((*traits).cacheInvRootMatvec)
              {
                *invRootMatvecPart = (*matrix).multiplyRoot(*invMatvecPart);
                invRootMatvecValid = true;
              }

              stochasticPart = *invMatvecPart;
              invMatvecValid = false;
            }
            else
            {
              stochasticPart = (*matrix).multiplyInverse(stochasticPart);

              if ((*traits).cacheInvMatvec)
                invMatvecValid = false;

              if ((*traits).cacheInvRootMatvec)
                invRootMatvecValid = false;
            }

            trendPart.timesInverseMatrix();
          }

          /**
           * @brief Multiply random field with approximate root of cov. matrix
           */
          void timesMatrixRoot()
          {
            if ((*traits).cacheInvMatvec && (*traits).cacheInvRootMatvec)
            {
              *invMatvecPart = *invRootMatvecPart;
              invMatvecValid = invRootMatvecValid;
            }

            if ((*traits).cacheInvRootMatvec)
            {
              *invRootMatvecPart = stochasticPart;
              invRootMatvecValid = true;
            }

            stochasticPart = (*matrix).multiplyRoot(stochasticPart);

            trendPart.timesMatrixRoot();
          }

          /**
           * @brief Multiply random field with approximate inverse root of cov. matrix
           */
          void timesInvMatRoot()
          {
            if ((*traits).cacheInvRootMatvec && invRootMatvecValid)
            {
              stochasticPart = *invRootMatvecPart;
              invRootMatvecValid = false;

              if ((*traits).cacheInvMatvec)
              {
                *invRootMatvecPart = *invMatvecPart;
                invRootMatvecValid = invMatvecValid;
                invMatvecValid  = false;
              }
            }
            else
            {
              stochasticPart = (*matrix).multiplyInverse(stochasticPart);

              if ((*traits).cacheInvRootMatvec)
              {
                *invRootMatvecPart = stochasticPart;
                invRootMatvecValid = true;
              }

              stochasticPart = (*matrix).multiplyRoot(stochasticPart);

              if ((*traits).cacheInvMatvec)
                invMatvecValid = false;
            }

            trendPart.timesInvMatRoot();
          }

          RF oneNorm() const
          {
            return trendPart.oneNorm() + stochasticPart.oneNorm();
          }

          RF twoNorm() const
          {
            return std::sqrt( *this * *this);
          }

          RF infNorm() const
          {
            return std::max(trendPart.infNorm(), stochasticPart.infNorm());
          }

          bool operator==(const RandomField& other) const
          {
            return (trendPart == other.trendPart && stochasticPart == other.stochasticPart);
          }

          bool operator!=(const RandomField& other) const
          {
            return !operator==(other);
          }

          void localize(const typename Traits::DomainType& center, const RF radius)
          {
            stochasticPart.localize(center,radius);

            if ((*traits).cacheInvMatvec)
              invMatvecValid = false;

            if ((*traits).cacheInvRootMatvec)
              invRootMatvecValid = false;
          }
      };

    /**
     * @brief List of Gaussian random fields in 1D, 2D or 3D
     */
    template<typename GridTraits, template<typename> class Matrix = RandomFieldMatrix,
      template<typename, template<typename> class> class RandomField = Dune::RandomField::RandomField>
      class RandomFieldList
      {
        public:

          using SubRandomField = RandomField<GridTraits,Matrix>;

        protected:

          // to allow reading in constructor
          class ParamTreeHelper
          {
            Dune::ParameterTree config;

            public:

            ParamTreeHelper(const std::string& fileName = "")
            {
              if (fileName != "")
              {
                Dune::ParameterTreeParser parser;
                parser.readINITree(fileName+".fieldList",config);
              }
            }

            const Dune::ParameterTree& get() const
            {
              return config;
            }
          };

          const ParamTreeHelper     treeHelper;
          const Dune::ParameterTree config;
          std::vector<std::string>  fieldNames;
          std::vector<std::string>  activeTypes;

          std::map<std::string, std::shared_ptr<SubRandomField> > list;
          std::shared_ptr<SubRandomField> emptyPointer;

          using Traits = RandomFieldTraits<GridTraits,Matrix>;
          using RF     = typename Traits::RF;

        public:

          /**
           * @brief Constructor reading random fields from file
           */
          template<typename LoadBalance = DefaultLoadBalance<Traits::dim> >
            RandomFieldList(
                const Dune::ParameterTree& config_,
                const std::string& fileName = "",
                const LoadBalance loadBalance = LoadBalance(),
                const MPI_Comm comm = MPI_COMM_WORLD
                )
            : config(config_)
            {
              std::stringstream typeStream(config.get<std::string>("randomField.types"));
              std::string type;
              while(std::getline(typeStream, type, ' '))
              {
                fieldNames.push_back(type);

                Dune::ParameterTree subConfig;
                Dune::ParameterTreeParser parser;
                parser.readINITree(type+".field",subConfig);

                // copy general keys to subConfig if necessary
                if (!subConfig.hasKey("grid.extensions") && config.hasKey("grid.extensions"))
                  subConfig["grid.extensions"] = config["grid.extensions"];
                if (!subConfig.hasKey("grid.cells") && config.hasKey("grid.cells"))
                  subConfig["grid.cells"] = config["grid.cells"];
                if (!subConfig.hasKey("randomField.cgIterations")
                    && config.hasKey("randomField.cgIterations"))
                  subConfig["randomField.cgIterations"] = config["randomField.cgIterations"];

                std::string subFileName = fileName;
                if (subFileName != "")
                  subFileName += "." + type;

                list.insert({type,
                    std::make_shared<SubRandomField>(subConfig,subFileName,loadBalance,comm)});
              }

              if (fieldNames.empty())
                DUNE_THROW(Dune::Exception,"List of randomField types is empty");

              activateFields(config.get<int>("randomField.active",fieldNames.size()));
            }

          /**
           * @brief Constructor reading random fields and config from file
           */
          template<typename LoadBalance = DefaultLoadBalance<Traits::dim> >
            RandomFieldList(
                const std::string& fileName,
                const LoadBalance loadBalance = LoadBalance(),
                const MPI_Comm comm = MPI_COMM_WORLD
                )
            : treeHelper(fileName), config(treeHelper.get())
            {
              std::stringstream typeStream(config.get<std::string>("randomField.types"));
              std::string type;
              while(std::getline(typeStream, type, ' '))
              {
                fieldNames.push_back(type);

                std::string subFileName = fileName + "." + type;

                list.insert({type, std::make_shared<SubRandomField>(subFileName,loadBalance,comm)});
              }

              if (fieldNames.empty())
                DUNE_THROW(Dune::Exception,"List of randomField types is empty");

              activateFields(config.get<int>("randomField.active",fieldNames.size()));
            }

          /**
           * @brief Constructor reading random fields from file, but reusing covariance matrices
           */
          RandomFieldList(const RandomFieldList& other, const std::string& fileName)
            : fieldNames(other.fieldNames), activeTypes(other.activeTypes)
          {
            for(const std::pair<std::string,std::shared_ptr<SubRandomField> >& pair : other.list)
              list.insert({pair.first,
                  std::make_shared<SubRandomField>(*(pair.second),fileName + "." + pair.first)});
          }

#if HAVE_DUNE_PDELAB
           /**
           * @brief Constructor converting from GridFunctionSpace and GridVector
           */
          template<typename GFS, typename FieldList>
            RandomFieldList(const RandomFieldList& other, const GFS& gfs, const FieldList& fieldList)
            : fieldNames(other.fieldNames), activeTypes(other.activeTypes)
            {
              for (const std::string& type : activeTypes)
              {
                if (fieldList.find(type) == fieldList.end())
                  DUNE_THROW(Dune::Exception,"Field name " + type + " not found in grid function list");

                std::shared_ptr<SubRandomField> otherField = other.list.find(type)->second;
                list.insert({type,
                    std::make_shared<SubRandomField>(*otherField,gfs,*(fieldList.find(type)->second))});
              }

              for (const std::string& type : fieldNames)
                if (fieldList.find(type) == fieldList.end())
                  list.insert({type,
                      std::make_shared<SubRandomField>(*(other.list.find(type)->second))});
            }

           /**
           * @brief Constructor converting from DiscreteGridFunction map
           */
          template<typename DGFList>
            RandomFieldList(const RandomFieldList& other, const DGFList& dgfList)
            : fieldNames(other.fieldNames), activeTypes(other.activeTypes)
            {
              for (const std::string& type : activeTypes)
              {
                if (dgfList.find(type) == dgfList.end())
                  DUNE_THROW(Dune::Exception,"Field name " + type + " not found in grid function list");

                std::shared_ptr<SubRandomField> otherField = other.list.find(type)->second;
                list.insert({type,
                    std::make_shared<SubRandomField>(*otherField,*(dgfList.find(type)->second))});
              }

              for (const std::string& type : fieldNames)
                if (dgfList.find(type) == dgfList.end())
                  list.insert({type,
                      std::make_shared<SubRandomField>(*(other.list.find(type)->second))});
            }
#endif // HAVE_DUNE_PDELAB

          /**
           * @brief Copy constructor
           */
          RandomFieldList(const RandomFieldList& other)
            : fieldNames(other.fieldNames), activeTypes(other.activeTypes)
          {
            for(const std::pair<std::string,std::shared_ptr<SubRandomField> >& pair : other.list)
              list.insert({pair.first, std::make_shared<SubRandomField>(*(pair.second))});
          }

          /**
           * @brief Assignment operator
           */
          RandomFieldList& operator=(const RandomFieldList& other)
          {
            if (this != &other)
            {
              fieldNames  = other.fieldNames;
              activeTypes = other.activeTypes;

              list.clear();
              for(const std::pair<std::string,std::shared_ptr<SubRandomField> >& pair : other.list)
                list.insert({pair.first, std::make_shared<SubRandomField>(*(pair.second))});
            }

            return *this;
          }

          /**
           * @brief Insert additional random field into list
           */
          void insert(const std::string& type, const SubRandomField& field, bool activate = true)
          {
            fieldNames.push_back(type);
            if (activate)
              activeTypes.push_back(type);

            list.insert({type, std::make_shared<SubRandomField>(field)});
          }

          /**
           * @brief Define subset of fields kept constant (i.e. not changed by calculus operators)
           */
          void activateFields(const unsigned int number)
          {
            if (number > fieldNames.size())
              DUNE_THROW(Dune::Exception,"Too many randomFields activated");

            activeTypes.clear();
            for (unsigned int i = 0; i < number; i++)
              activeTypes.push_back(fieldNames[i]);
          }

          /**
           * @brief Number of degrees of freedom (for active types)
           */
          unsigned int dofs() const
          {
            unsigned int output = 0;

            for (const std::string& type : activeTypes)
              output += list.find(type)->second->dofs();

            return output;
          }

          /**
           * @brief Generate fields with the desired correlation structure
           */
          void generate(bool allowNonWorldComm = false)
          {
            for(const std::string& type : fieldNames)
              list.find(type)->second->generate(allowNonWorldComm);
          }

          /**
           * @brief Generate fields without correlation structure (i.e. noise)
           */
          void generateUncorrelated(bool allowNonWorldComm = false)
          {
            for(const std::string& type : fieldNames)
              list.find(type)->second->generateUncorrelated(allowNonWorldComm);
          }

          /**
           * @brief Vector of random field types currently active
           */
          const std::vector<std::string> types() const
          {
            return activeTypes;
          }

          /**
           * @brief Access to individual random field
           */
          const std::shared_ptr<SubRandomField>& get(const std::string& type) const
          {
            if (list.find(type) != list.end())
              return (list.find(type))->second;

            return emptyPointer;
          }

          /**
           * @brief Export random fields to files on disk
           */
          void writeToFile(const std::string& fileName) const
          {
            for(const std::string& type : fieldNames)
              list.find(type)->second->writeToFile(fileName + "." + type);

            std::ofstream file(fileName+".fieldList",std::ofstream::trunc);
            config.report(file);
          }

          /**
           * @brief Export random fields as flat unstructured VTK files, requires dune-grid and dune-functions
           */
          template<typename GridView>
            void writeToVTK(const std::string& fileName, const GridView& gv) const
            {
#if HAVE_DUNE_FUNCTIONS
              for (const std::string& type : fieldNames)
                list.find(type)->second->writeToVTK(fileName + "." + type,gv);
#else //HAVE_DUNE_FUNCTIONS
              DUNE_THROW(Dune::NotImplemented,"Unstructured VTK output requires dune-grid and dune-functions");
#endif //HAVE_DUNE_FUNCTIONS
            }

          /**
           * @brief Export random fields as unstructured VTK files, requires dune-grid and dune-functions
           */
          template<typename GridView>
            void writeToVTKSeparate(const std::string& fileName, const GridView& gv) const
            {
#if HAVE_DUNE_FUNCTIONS
              for (const std::string& type : fieldNames)
                list.find(type)->second->writeToVTKSeparate(fileName + "." + type,gv);
#else //HAVE_DUNE_FUNCTIONS
              DUNE_THROW(Dune::NotImplemented,"Unstructured VTK output requires dune-grid and dune-functions");
#endif //HAVE_DUNE_FUNCTIONS
            }

          /**
           * @brief Export random fields as flat Legacy VTK files
           */
          void writeToLegacyVTK(const std::string& fileName) const
          {
            for (const std::string& type : fieldNames)
              list.find(type)->second->writeToLegacyVTK(fileName + "." + type);
          }

          /**
           * @brief Export random fields as separate Legacy VTK entries
           */
          void writeToLegacyVTKSeparate(const std::string& fileName) const
          {
            for (const std::string& type : fieldNames)
              list.find(type)->second->writeToLegacyVTKSeparate(fileName + "." + type);
          }

          /**
           * @brief Set the random fields to zero
           */
          void zero()
          {
            for(const std::string& type : activeTypes)
              list.find(type)->second->zero();
          }

          /**
           * @brief Double spatial resolution of covariance matrix
           */
          void refineMatrix()
          {
            for(const std::string& type : activeTypes)
              list.find(type)->second->refineMatrix();
          }

          /**
           * @brief Double spatial resolution of random fields
           */
          void refine()
          {
            for(const std::string& type : activeTypes)
              list.find(type)->second->refine();
          }

          /**
           * @brief Reduce spatial resolution of covariance matrix
           */
          void coarsenMatrix()
          {
            for(const std::string& type : activeTypes)
              list.find(type)->second->coarsenMatrix();
          }

          /**
           * @brief Reduce spatial resolution of random fields
           */
          void coarsen()
          {
            for(const std::string& type : activeTypes)
              list.find(type)->second->coarsen();
          }

          /**
           * @brief Addition assignment operator
           */
          RandomFieldList& operator+=(const RandomFieldList& other)
          {
            for(const std::string& type : activeTypes)
            {
              if (other.list.find(type) == other.list.end())
                DUNE_THROW(Dune::Exception,"RandomFieldLists don't match in operator+=");

              list.find(type)->second->operator+=(*(other.list.find(type)->second));
            }

            return *this;
          }

          /**
           * @brief Subtraction assignment operator
           */
          RandomFieldList& operator-=(const RandomFieldList& other)
          {
            for(const std::string& type : activeTypes)
            {
              if (other.list.find(type) == other.list.end())
                DUNE_THROW(Dune::Exception,"RandomFieldLists don't match in operator+=");

              list.find(type)->second->operator-=(*(other.list.find(type)->second));
            }

            return *this;
          }

          /**
           * @brief Multiplication with scalar
           */
          RandomFieldList& operator*=(const RF alpha)
          {
            for(const std::string& type : activeTypes)
              list.find(type)->second->operator*=(alpha);

            return *this;
          }

          /**
           * @brief AXPY scaled addition
           */
          RandomFieldList& axpy(const RandomFieldList& other, const RF alpha)
          {
            for(const std::string& type : activeTypes)
            {
              if (other.list.find(type) == other.list.end())
                DUNE_THROW(Dune::Exception,"RandomFieldLists don't match in axpy");

              list.find(type)->second->axpy(*(other.list.find(type)->second),alpha);
            }

            return *this;
          }

          RandomFieldList& axpy(const RF alpha, const RandomFieldList& other)
          {
            return axpy(other,alpha);
          }

          /**
           * @brief Scalar product
           */
          RF operator*(const RandomFieldList& other) const
          {
            RF output = 0.;

            for(const std::string& type : activeTypes)
            {
              if (other.list.find(type) == other.list.end())
                DUNE_THROW(Dune::Exception,"RandomFieldLists don't match in operator*");

              output += list.find(type)->second->operator*(*(other.list.find(type)->second));
            }

            return output;
          }

          /**
           * @brief Multiply random fields with covariance matrix
           */
          void timesMatrix()
          {
            for(const std::string& type : activeTypes)
              list.find(type)->second->timesMatrix();
          }

          /**
           * @brief Multiply random fields with inverse of covariance matrix
           */
          void timesInverseMatrix()
          {
            for(const std::string& type : activeTypes)
              list.find(type)->second->timesInverseMatrix();
          }

          /**
           * @brief Multiply random fields with approximate root of cov. matrix
           */
          void timesMatrixRoot()
          {
            for(const std::string& type : activeTypes)
              list.find(type)->second->timesMatrixRoot();
          }

          /**
           * @brief Multiply random fields with approximate inverse root of cov. matrix
           */
          void timesInvMatRoot()
          {
            for(const std::string& type : activeTypes)
              list.find(type)->second->timesInvMatRoot();
          }

          RF oneNorm() const
          {
            RF sum = 0.;
            for(const std::string& type : activeTypes)
              sum += list.find(type)->second->oneNorm();

            return sum;
          }

          RF twoNorm() const
          {
            RF sum = 0.;
            for(const std::string& type : activeTypes)
              sum += std::pow(list.find(type)->second->twoNorm(),2);

            return std::sqrt(sum);
          }

          RF infNorm() const
          {
            RF max = 0;
            for (const std::string& type : activeTypes)
              max = std::max(max, list.find(type)->second->infNorm());

            return max;
          }

          bool operator==(const RandomFieldList& other) const
          {
            bool same = true;
            for (const std::string& type : fieldNames)
            {
              if (other.list.find(type) == other.list.end())
                DUNE_THROW(Dune::Exception,"RandomFieldLists don't match in operator==");

              if (!(list.find(type)->second->operator==(*(other.list.find(type)->second))))
              {
                same = false;
                break;
              }
            }

            return same;
          }

          bool operator!=(const RandomFieldList& other) const
          {
            return !operator==(other);
          }

          void localize(const typename GridTraits::Domain& center, const RF radius)
          {
            for(const std::string& type : activeTypes)
              list.find(type)->second->localize(center,radius);
          }

      };
  }
}

#endif // DUNE_RANDOMFIELD_RANDOMFIELD_HH

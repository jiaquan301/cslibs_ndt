#ifndef CSLIBS_CSLIBS_NDT_STATIC_GRIDMAP_HPP
#define CSLIBS_CSLIBS_NDT_STATIC_GRIDMAP_HPP

#include <array>
#include <vector>
#include <cmath>
#include <memory>

#include <cslibs_math_2d/linear/pose.hpp>
#include <cslibs_math_2d/linear/point.hpp>

#include <cslibs_ndt/common/distribution_container.hpp>

#include <cslibs_math/common/array.hpp>

#include <cslibs_indexed_storage/storage.hpp>
#include <cslibs_indexed_storage/backend/array/array.hpp>


namespace cis = cslibs_indexed_storage;

namespace cslibs_ndt {
namespace static_maps {
template<bool limit_covariance = false>
class Gridmap
{
public:
    using Ptr                   = std::shared_ptr<Gridmap>;
    using pose_t                = cslibs_math_2d::Pose2d;
    using index_t               = std::array<int, 2>;
    using mutex_t               = std::mutex;
    using lock_t                = std::unique_lock<mutex_t>;
    using distribution_container_t  = DistributionContainer<2>;
    using storage_t                 = cis::Storage<distribution_container_t, index_t, cis::backend::array::Array>;

    Gridmap(const pose_t   &origin,
            const double    resolution,
            const double    height,
            const double    width) :
        resolution_(resolution),
        resolution_inv_(1.0 / resolution_),
        w_T_m_(origin),
        m_T_w_(w_T_m_.inverse()),
        min_index_{{static_cast<int>(std::floor(origin.tx() * resolution_inv_),
                                     std::floor(origin.ty() * resolution_inv_))}},
        max_index_{{static_cast<int>(std::floor((origin.tx() + width)  * resolution_inv_),
                                     std::floor((origin.ty() + height) * resolution_inv_))}},
        storage_(new storage_t)
    {
        storage_->template set<cis::option::tags::array_offset>(min_index_[0],
                                                                min_index_[1]);
        storage_->template set<cis::option::tags::array_size>(static_cast<std::size_t>(max_index_[0] - min_index_[0] + 1),
                                                              static_cast<std::size_t>(max_index_[1] - min_index_[1] + 1));
    }

    Gridmap(const double origin_x,
            const double origin_y,
            const double origin_phi,
            const double resolution,
            const double height,
            const double width) :
        resolution_(resolution),
        resolution_inv_(1.0 / resolution_),
        w_T_m_(origin_x, origin_y, origin_phi),
        m_T_w_(w_T_m_.inverse()),
        min_index_{{static_cast<int>(std::floor(origin_x * resolution_inv_),
                                     std::floor(origin_y * resolution_inv_))}},
        max_index_{{static_cast<int>(std::floor((origin_x + width)  * resolution_inv_),
                                     std::floor((origin_y + height) * resolution_inv_))}},
        storage_(new storage_t)
    {
        storage_->template set<cis::option::tags::array_offset>(min_index_[0],
                                                                min_index_[1]);
        storage_->template set<cis::option::tags::array_size>(static_cast<std::size_t>(max_index_[0] - min_index_[0] + 1),
                                                              static_cast<std::size_t>(max_index_[1] - min_index_[1] + 1));
    }

    inline cslibs_math_2d::Point2d getMin() const
    {
        lock_t l(storage_mutex_);
        return cslibs_math_2d::Point2d(w_T_m_.translation());
    }

    inline cslibs_math_2d::Point2d getMax() const
    {
        lock_t l(storage_mutex_);
        return cslibs_math_2d::Point2d(w_T_m_.tx() + width_,
                                       w_T_m_.ty() + height_);
    }

    inline cslibs_math_2d::Pose2d getOrigin() const
    {
        return w_T_m_;
    }

    inline void add(const cslibs_math_2d::Point2d &point)
    {
        distribution_container_t::handle_t distribution;
        {
            lock_t l(storage_mutex_);
            const index_t index = toIndex(point);
            distribution = distribution_container_t::handle_t(storage_->get(index));
            if(distribution.empty()) {
                distribution = distribution_container_t::handle_t(&(storage_->insert(index, distribution_container_t())));
            }
        }
        distribution->data().add(point);
        distribution->setTouched();
    }

    inline double sample(const cslibs_math_2d::Point2d &point) const
    {

        const index_t index                = toIndex(point);
        const distribution_container_t::handle_t distribution = getDistribution(index);
        auto  get = [distribution, &point](){
            double p = distribution->data().sample(point);
            return p;
        };
        return distribution.empty() ? 0.0 : get();
    }

    inline double sampleNonNormalized(const cslibs_math_2d::Point2d &point) const
    {

        const index_t index                = toIndex(point);
        const distribution_container_t::handle_t distribution = getDistribution(index);
        auto  get = [distribution, &point](){
            double p = distribution->data().sampleNonNormalized(point);
            return p;
        };
        return distribution.empty() ? 0.0 : get();
    }

    inline index_t getMinIndex() const
    {
        lock_t l(storage_mutex_);
        return min_index_;
    }

    inline index_t getMaxIndex() const
    {
        lock_t l(storage_mutex_);
        return max_index_;
    }

    inline distribution_container_t::handle_t const getDistribution(const index_t &distribution_index) const
    {
        lock_t l(storage_mutex_);
        return distribution_container_t::handle_t(storage_->get(distribution_index));
    }

    inline distribution_container_t::handle_t getDistribution(const index_t &distribution_index)
    {
        lock_t l(storage_mutex_);
        return distribution_container_t::handle_t(storage_->get(distribution_index));
    }

    inline double getResolution() const
    {
        return resolution_;
    }

protected:
    const double                        resolution_;
    const double                        resolution_inv_;
    const cslibs_math_2d::Transform2d   w_T_m_;
    const cslibs_math_2d::Transform2d   m_T_w_;
    const double                        height_;
    const double                        width_;

    mutable index_t                     min_index_;
    mutable index_t                     max_index_;
    mutable mutex_t                     storage_mutex_;
    mutable std::shared_ptr<storage_t>  storage_;


    inline index_t toIndex(const cslibs_math_2d::Point2d &p_w) const
    {
        const cslibs_math_2d::Point2d p_m = m_T_w_ * p_w;
        return {{static_cast<int>(p_m(0) * resolution_inv_),
                 static_cast<int>(p_m(1) * resolution_inv_)}};
    }
};
}
}



#endif // CSLIBS_GRIDMAPS_DYNAMIC_GRIDMAP_HPP
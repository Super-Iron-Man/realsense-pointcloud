#ifndef _NDT_EDGE_BASED_REGISTRATION_H
#define _NDT_EDGE_BASED_REGISTRATION_H

#include "types.hpp"
#include "edge_extractor.hpp"

class NDTEdgeBasedRegistration: public TwoPhaseRegistrationScheme {
public:
    NDTEdgeBasedRegistration(): TwoPhaseRegistrationScheme() {}
    NDTEdgeBasedRegistration(std::vector<float3>& input_thetas): TwoPhaseRegistrationScheme() {
        thetas = input_thetas;
        use_imu = true;
    }
    NDTEdgeBasedRegistration(float usr_def_rads): TwoPhaseRegistrationScheme() {
        rads = usr_def_rads;
    }

    rgb_point_cloud_pointer extract_features(rgb_point_cloud_pointer cloud) {
        return extract_edge_features(cloud);
    }

    // Given a vector<pair<feature_cloud, original_cloud>>, compute a global point cloud
    rgb_point_cloud_pointer global_registration(std::vector<std::pair<rgb_point_cloud_pointer, rgb_point_cloud_pointer>>& clouds) {
        std::cout << "[PCL] Performing edge-based registration";
        if (use_imu) {
            std::cout << " with dynamic initial rotation guesses..." << std::endl;
            assert(clouds.size() == thetas.size());
        } else {
            std::cout << " with static initial rotation guesses..." << std::endl;
        }

        pcl::IterativeClosestPoint<rgb_point, rgb_point> icp;
        pcl::registration::CorrespondenceRejectorTrimmed::Ptr cor_rej_trimmed(new pcl::registration::CorrespondenceRejectorTrimmed);
        pcl::ApproximateVoxelGrid<rgb_point> approx_voxel_grid;

        float acc_rads = 0.;

        pcl::NormalDistributionsTransform<pcl::PointXYZRGB, pcl::PointXYZRGB> ndt;
        ndt.setTransformationEpsilon(0.01);
        ndt.setStepSize(0.1);
        ndt.setResolution(1.0);

        ndt.setMaximumIterations(50);

        approx_voxel_grid.setLeafSize(0.01, 0.01, 0.01);

        icp.setMaximumIterations(100);
        icp.setMaxCorrespondenceDistance(0.01);
        icp.setTransformationEpsilon(1);
        icp.setEuclideanFitnessEpsilon(1000);

        rgb_point_cloud_pointer target_cloud = clouds[0].first;
        rgb_point_cloud_pointer global_cloud(new rgb_point_cloud);

        *global_cloud = *global_cloud + *clouds[0].second;

        approx_voxel_grid.setInputCloud(target_cloud);
        approx_voxel_grid.filter(*target_cloud);

        // these cloud pointers are to be used as temporary variables
        rgb_point_cloud_pointer downsized_src(new rgb_point_cloud);
        rgb_point_cloud_pointer downsized_dst(new rgb_point_cloud);

        for (int cloud_idx = 1; cloud_idx < (int)clouds.size(); cloud_idx++) {
            rgb_point_cloud_pointer aligned(new rgb_point_cloud);
            rgb_point_cloud_pointer icp_aligned(new rgb_point_cloud);

            approx_voxel_grid.setInputCloud(clouds[cloud_idx].first);
            approx_voxel_grid.filter(*downsized_src);

            ndt.setInputSource(downsized_src);
            ndt.setInputTarget(target_cloud);

            Eigen::Translation3f init_translation(0, 0, 0);
            if (use_imu) {
                float3 absolute_theta = thetas[0] * -1.0;
                thetas[cloud_idx].add(absolute_theta.x, absolute_theta.y, absolute_theta.z);

                Eigen::AngleAxisf init_rotation_y_dynamic(-thetas[cloud_idx].y, Eigen::Vector3f::UnitY());
                Eigen::Matrix4f init_guess = (init_translation * init_rotation_y_dynamic).matrix();

                std::cout << "[PCL]   Performing NDT iteration [" << cloud_idx << "]..." << std::flush;
                ndt.align(*aligned, init_guess);
                std::cout << "OK" << std::endl;
            } else {
                acc_rads += rads;

                Eigen::AngleAxisf init_rotation_y_static(acc_rads, Eigen::Vector3f::UnitY());
                Eigen::Matrix4f init_guess = (init_translation * init_rotation_y_static).matrix();

                std::cout << "[PCL]   Performing NDT iteration [" << cloud_idx << "]..." << std::flush;
                ndt.align(*aligned, init_guess);
                std::cout << "OK" << std::endl;
            }

            icp.setInputSource(aligned);
            icp.setInputTarget(target_cloud);
            std::cout << "[PCL]   Performing ICP iteration [" << cloud_idx << "]..." << std::flush;
            icp.align(*icp_aligned);

            if (icp.hasConverged()) {
                std::cout << "OK" << std::endl;
                rgb_point_cloud_pointer transformed(new rgb_point_cloud);
                pcl::transformPointCloud(*clouds[cloud_idx].second, *transformed, ndt.getFinalTransformation());
                pcl::transformPointCloud(*transformed, *transformed, icp.getFinalTransformation());

                *target_cloud = *icp_aligned + *target_cloud;
                *global_cloud = *global_cloud + *transformed;
            } else {
                std::cout << std::endl;
            }
        }

        std::cout << "[PCL] Done" << std::endl;

        return global_cloud;
    }

private:
    bool use_imu = false;
    std::vector<float3> thetas;
    float rads = -0.523599;
};

#endif

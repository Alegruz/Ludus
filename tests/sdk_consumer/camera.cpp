#include <ludus/gameplay/camera/evaluation.hpp>

bool ExerciseInstalledCamera() noexcept
{
    using namespace ludus::gameplay::camera;
    const CameraRigDefinition definition{ .Recipe = CameraRecipe::OrthographicFollow, .FollowOffset = {2, 3} };
    const CameraTargetSample target{ .Position = {1.0e12 + 0.25, 4, 0} };
    CameraSample sample;
    CameraTrace trace;
    return TryEvaluateCamera(definition, { .OutputId = 7 }, &target, sample, &trace) == CameraStatus::Success &&
           sample.Pose.Position.X == 1.0e12 + 2.25 && sample.Pose.Position.Y == 7 && sample.OutputId == 7 &&
           trace.Published.OutputId == 7 && ValidateCameraSample(sample) == CameraStatus::Success;
}

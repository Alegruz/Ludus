// libFuzzer entry point: the pbrt reader (without file access), the PLY reader
// and the image decoders must reject or accept any input without crashing.

#include "importers/image/image_reader.h"
#include "importers/pbrt/pbrt_parser.h"
#include "importers/pbrt/ply_reader.h"

extern "C" int LLVMFuzzerTestOneInput(const ludus::foundation::core::uint8* data, ludus::foundation::core::usize size)
{
    using namespace ludus::content::scene_cook::importers;
    const std::span<const ludus::foundation::core::uint8> input(data, size);
    SceneIr scene;
    DiagnosticLog log;
    (void)pbrt::ReadSceneFromMemory({input, {}, "fuzz.pbrt"}, scene, log);
    IrMesh mesh;
    PlyHeaderSummary summary;
    std::string_view message;
    (void)ReadPly(input, mesh, summary, message);
    DecodedImage image;
    (void)DecodeImage("fuzz.tga", input, image, message);
    (void)DecodeImage("fuzz.pfm", input, image, message);
    (void)DecodeImage("fuzz.png", input, image, message);
    return 0;
}

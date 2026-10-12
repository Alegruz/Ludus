#include "internal/render_load_job.h"

namespace ludus::editor
{
RenderLoadJob::RenderLoadJob(const QString& root,
                             const WorldDocument& world,
                             foundation::uint64 generation,
                             bool launch,
                             QObject* parent)
    : QObject(parent), Root_(root), World_(world), Generation_(generation), Launch_(launch)
{
    auto* worker = new QObject;
    worker->moveToThread(&Thread_);
    connect(&Thread_, &QThread::started, worker, [this]() {
        Success_ = RenderScene::Load(Root_, World_, Scene_, Error_);
        Thread_.quit();
    });
    connect(&Thread_, &QThread::finished, worker, &QObject::deleteLater);
    connect(&Thread_, &QThread::finished, this, &RenderLoadJob::Finished);
    Thread_.start();
}
RenderLoadJob::~RenderLoadJob()
{
    Thread_.quit();
    // CPU input sizes/counts are bounded. Joining precedes destruction of the
    // snapshot/result members captured by the worker, including project close.
    (void)Thread_.wait();
}
} // namespace ludus::editor

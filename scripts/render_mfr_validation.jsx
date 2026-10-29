// Caller supplies mfrValidationConfig = {enabled, bitDepth, directory, compId}.
(function () {
    var cfg = mfrValidationConfig;
    var c = null;
    for (var i = 1; i <= app.project.numItems; i++) if (app.project.item(i).id === cfg.compId) c = app.project.item(i);
    if (!c || c.name !== "__有序图案_多帧验证") throw new Error("Validation composition missing.");
    if (app.project.renderQueue.numItems > 1) throw new Error("Unexpected render queue items.");
    if (app.project.renderQueue.numItems === 1) {
        if (app.project.renderQueue.item(1).comp.id !== cfg.compId) throw new Error("Unexpected queued composition.");
        app.project.renderQueue.item(1).remove();
    }
    app.project.bitsPerChannel = cfg.bitDepth;
    var q = app.project.renderQueue.items.add(c);
    q.timeSpanStart = 0;
    q.timeSpanDuration = .96;
    q.outputModule(1).applyTemplate(cfg.bitDepth === 16 ? "_HIDDEN X-Factor 16" : "_HIDDEN X-Factor 8");
    q.outputModule(1).file = new File(cfg.directory + "/frame_[#####].png");
    var outputSettings = q.outputModule(1).getSettings(GetSettingsFormat.STRING);
    app.purge(PurgeTarget.ALL_CACHES);
    app.setMultiFrameRenderingConfig(cfg.enabled, 100);
    var start = new Date().getTime();
    app.project.renderQueue.render();
    var result = {enabled: cfg.enabled, bitDepth: cfg.bitDepth, elapsedMs: new Date().getTime() - start,
        done: q.status === RQItemStatus.DONE, status: q.status, output: q.outputModule(1).file.fsName,
        depth: outputSettings.Depth, format: outputSettings.Format};
    if (!result.done) throw new Error("Render did not complete: " + JSON.stringify(result));
    return JSON.stringify(result);
}());

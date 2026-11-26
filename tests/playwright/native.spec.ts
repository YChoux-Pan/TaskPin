import { test, expect } from '@playwright/test';
import { spawn } from 'node:child_process';
import fs from 'node:fs/promises';
import path from 'node:path';
import os from 'node:os';

// TaskPin is a QWidget application. Playwright Test orchestrates its real native
// fixtures; no browser facade, desktop mouse, Win32 input, or cloud key is used.
const build = path.resolve(process.env.TASKPIN_BUILD_DIR || 'TaskPinQt/build');
const temporary = new Set<string>();
async function workspace() {
  const folder = await fs.mkdtemp(path.join(os.tmpdir(), 'taskpin-pw-'));
  temporary.add(folder); return folder;
}
test.afterEach(async () => {
  for (const folder of temporary) {
    const relative = path.relative(os.tmpdir(), folder);
    if (relative.startsWith('taskpin-pw-') && !relative.includes(path.sep))
      await fs.rm(folder, { recursive: true, force: true });
  }
  temporary.clear();
});

async function native(exe: string, args: string[], output: string, overrides: Record<string, string> = {}) {
  await fs.mkdir(output, { recursive: true });
  const env = { ...process.env, QT_QPA_PLATFORM: 'offscreen', TASKPIN_AUTOMATION: '1',
    TASKPIN_DATA: path.join(output, 'isolated-data'), ...overrides };
  const child = spawn(path.join(build, exe + '.exe'), args, { cwd: build, env, windowsHide: true });
  let stdout = '', stderr = '';
  child.stdout.on('data', value => stdout += value.toString());
  child.stderr.on('data', value => stderr += value.toString());
  const deadline = setTimeout(() => child.kill(), 165_000);
  const code = await new Promise<number | null>((resolve, reject) => {
    child.once('error', reject); child.once('exit', resolve);
  }).finally(() => clearTimeout(deadline));
  await fs.writeFile(path.join(output, 'process.txt'), `${stdout}\n${stderr}`, 'utf8');
  await test.info().attach(exe + ' process output', { path: path.join(output, 'process.txt'), contentType: 'text/plain' });
  return code;
}

async function qt(exe: string, slots: string[], info: any, overrides: Record<string, string> = {}) {
  const output = await workspace();
  await fs.mkdir(output, { recursive: true });
  const report = path.join(output, 'qt.txt');
  const junit = path.join(output, 'qt.xml');
  const code = await native(exe, [...slots, '-o', report + ',txt', '-o', junit + ',junitxml'], output, overrides);
  await info.attach('Qt native assertions', { path: report, contentType: 'text/plain' });
  await info.attach('Qt JUnit', { path: junit, contentType: 'application/xml' });
  const text = await fs.readFile(report, 'utf8');
  expect(code, text).toBe(0);
  expect(text).toMatch(/Totals: \d+ passed, 0 failed, 0 skipped/);
  expect(text).not.toContain('Unknown test function');
}

test('F1 editor → real PaddleOCR → translation pin, no result popup', async ({}, info) => {
  const output = await workspace();
  expect(await native('TaskPinTranslationProbe', [output, '--paragraph', '--comparison'], output)).toBe(0);
  const report = JSON.parse(await fs.readFile(path.join(output, 'native-probe.json'), 'utf8'));
  expect(report.fixtureOnly).toBe(true);
  expect(report.cloudCalled).toBe(false);
  expect(report.framelessPin).toBe(true);
  expect(report.positionAndZoomPreserved).toBe(true);
  expect(report.comparisonCreated).toBe(false);
  expect(report.ocrEngine).toContain('PaddleOCR');
  expect(report.profileId).toBe('builtin-ppocrv5-mobile');
  expect(report.ocrStageMs.inference).toBeGreaterThanOrEqual(0);
  expect(report.recognitionScores.length).toBeGreaterThan(0);
  expect(report.ocrMs).toBeGreaterThan(0);
  expect(report.translationMs).toBeGreaterThanOrEqual(0);
  expect(report.comparisonUi.opaqueBackground).toBe(true);
  expect(report.comparisonUi.textHeightRatio).toBeGreaterThan(.6);
  expect(report.comparisonUi.paneWidthRatio).toBeGreaterThan(.9);
  expect(report.comparisonUi.paneWidthRatio).toBeLessThan(1.1);
  await info.attach('Stage timings and native state', { path: path.join(output, 'native-probe.json'), contentType: 'application/json' });
  for (const name of ['translation-pin-mock.png', 'comparison.png'])
    await info.attach(name, { path: path.join(output, name), contentType: 'image/png' });
});

for (const scale of ['1', '1.25', '1.5', '2']) {
  test(`Wheel reversal, cursor anchor and cache stability at display scale ${scale}`, async ({}, info) => {
    await qt('TaskPinExperienceTests', ['wheelReversesPendingAnimation', 'wheelKeepsImagePointUnderCursor',
      'shrinkingPinKeepsSmoothPixelsAndClearsPadding', 'cacheSettlingKeepsPixelsStable'], info,
      { QT_SCALE_FACTOR: scale });
  });
}

test('Comparison stays opaque and proportional on light/dark pins', async ({}, info) => {
  await qt('TaskPinExperienceTests', ['comparisonIsOpaqueAndUsesHeightForText'], info);
});

test('Rectangle toolbar stays visible and slider and keys change exported stroke width', async ({}, info) => {
  await qt('TaskPinScreenshotTests', ['rectangleToolbarStaysVisibleDuringStroke',
    'rectangleStrokeFollowsSliderAndArrowKeys'], info);
});

test('Sliders resize selected annotations and undo restores their exported appearance', async ({}, info) => {
  await qt('TaskPinScreenshotTests', ['sizeSliderEditsSelectedAnnotation',
    'selectedTextFontSliderRestoresPreviousSize', 'textFormattingUpdatesCurrentDraftAndExport'], info);
});

test('Whiteboard width controls change the stroke drawn at the displayed size', async ({}, info) => {
  await qt('TaskPinExperienceTests', ['whiteboardWidthControlsDrawWithDisplayedWidth'], info);
});

test('Width settings follow save and cancel and pin border changes apply to the current pin', async ({}, info) => {
  await qt('TaskPinIntegrationTests', ['widthSlidersSaveAndCancel', 'pinBorderSliderUpdatesOnlyCurrentPin'], info);
});

test('Draft text moves by its grip and committed labels move directly with undo and redo', async ({}, info) => {
  await qt('TaskPinScreenshotTests', ['draftTextGripMovesInputAndExport', 'committedLabelsDragInCreationTool'], info);
});

test('Number menus and arrow style cycling update exported annotations', async ({}, info) => {
  await qt('TaskPinScreenshotTests', ['numberMenusStyleSelectedAndNewMarks', 'activeArrowCyclesThroughMenuStyles',
    'tabTogglesLineAndRestoresArrowStyle'], info);
});

test('Rendered numbers preserve cutouts and arrows retain their selectable silhouettes', async ({}, info) => {
  await qt('TaskPinAnnotationTests', [], info);
});

test('Translation status draws red text over the captured background', async ({}, info) => {
  await qt('TaskPinTranslationTests', ['translationStatusDrawsRedTextOverCapture'], info);
});

test('F3 and custom editor shortcuts export the selected image', async ({}, info) => {
  await qt('TaskPinScreenshotTests', ['configuredPinExportsAnnotatedSelection',
    'configuredTranslationReplacesDefaultShortcut', 'bareCustomTranslationDoesNotConsumeTextInput',
    'configuredOutputCommitsFocusedText', 'actualPinFallbackAppearsAndWorksInEditor',
    'regionSelectionOwnsCrosshair'], info);
});

test('Host hotkey routing, persistence and collision checks match settings', async ({}, info) => {
  await qt('TaskPinIntegrationTests', ['editorTranslationHotkeyPersistsThroughDialog',
    'editorTranslationRejectsShortcutCollision', 'globalHotkeysProtectEditorCommands', 'pinHotkeyRoutesToEditorInsteadOfClipboard',
    'pinHotkeyWithoutEditorKeepsClipboardBehavior'], info);
});

test('OCR profile validation, save and cancel preserve configuration', async ({}, info) => {
  await qt('TaskPinTranslationTests', ['ocrProfileSelectionSavesAndCancelKeepsPrevious',
    'invalidOcrProfileDoesNotSaveEitherConfiguration', 'ocrTimingMetadataIsValidatedAndPreserved',
    'sharedOcrWorkerReusesModelAndHonorsCancelledOwners', 'invalidSavedOcrSelectionFailsVisiblyBeforeRuntimeFallback',
    'ocrModelHashFailureExplainsSelectedModelProblem', 'saturatedOcrQueueKeepsFailureIdDuringImmediateReplacement',
    'immediateOcrInputFailureKeepsCallerIdSnapshot', 'customOcrValidationKeepsUiResponsiveAndCancelPreservesFiles',
    'prewarmReusesSelectedModelForFirstScreenshot'], info);
});

test('Bundled lightweight PaddleOCR switches, persists and translates a selected region', async ({}, info) => {
  await qt('TaskPinTranslationTests', ['lightweightOcrSelectionSavesAndCancelKeepsPrevious',
    'lightweightOcrActuallySwitchesSharedWorker'], info);
  const output = await workspace();
  expect(await native('TaskPinTranslationProbe', [output, '--lightweight', '--comparison'], output)).toBe(0);
  const report = JSON.parse(await fs.readFile(path.join(output, 'native-probe.json'), 'utf8'));
  expect(report.profileId).toBe('builtin-ppocrv4-mobile');
  expect(report.ocrEngine).toBe('PaddleOCR PP-OCRv4');
  expect(report.state).toBe('PRESENTING');
  expect(report.framelessPin).toBe(true);
  expect(report.positionAndZoomPreserved).toBe(true);
  expect(report.cloudCalled).toBe(false);
  await info.attach('Lightweight native OCR and translation state', { path: path.join(output, 'native-probe.json'), contentType: 'application/json' });
  await info.attach('Lightweight translation pin', { path: path.join(output, 'translation-pin-mock.png'), contentType: 'image/png' });
  const oneShot = await workspace();
  expect(await native('TaskPinTranslationProbe', [oneShot, '--lightweight', '--benchmark', '--oneshot-ocr', '--single-sample'], oneShot)).toBe(0);
  const timing = JSON.parse(await fs.readFile(path.join(oneShot, 'benchmark.json'), 'utf8'));
  expect(timing.samples).toHaveLength(2);
  for (const sample of timing.samples) {
    expect(sample.profileId).toBe('builtin-ppocrv4-mobile');
    expect(sample.ocrEngine).toBe('PaddleOCR PP-OCRv4');
  }
  await info.attach('Lightweight one-shot model identity', { path: path.join(oneShot, 'benchmark.json'), contentType: 'application/json' });
});

test('Isolated application selftest keeps data and platform contracts', async ({}, info) => {
  const output = await workspace();
  expect(await native('TaskPinQt', ['--selftest', output], output)).toBe(0);
  const bytes = await fs.readFile(path.join(output, 'selftest.txt'));
  expect(bytes.toString('latin1')).toContain('53 passed, 0 failed');
  await info.attach('Selftest', { path: path.join(output, 'selftest.txt'), contentType: 'text/plain' });
});

test('PP-OCRv6 tiny recognizes the real selected image and creates a translation pin', async ({}, info) => {
  const output = await workspace();
  expect(await native('TaskPinTranslationProbe', [output, '--tiny', '--paragraph'], output)).toBe(0);
  const report = JSON.parse(await fs.readFile(path.join(output, 'native-probe.json'), 'utf8'));
  expect(report.profileId).toBe('builtin-ppocrv6-tiny');
  expect(report.ocrEngine).toBe('PaddleOCR PP-OCRv6');
  expect(report.state).toBe('PRESENTING');
  expect(report.framelessPin).toBe(true);
  expect(report.positionAndZoomPreserved).toBe(true);
  expect(report.cloudCalled).toBe(false);
  await info.attach('Actual v6 OCR metadata', { path: path.join(output, 'native-probe.json'), contentType: 'application/json' });
  await info.attach('V6 translation pin', { path: path.join(output, 'translation-pin-mock.png'), contentType: 'image/png' });
});

test('Copying a model pair after build discovers and uses it without recompilation', async ({}, info) => {
  await qt('TaskPinTranslationTests', ['v6OcrSelectionSavesAndReopens',
    'ocrFolderRefreshPreservesDraftAndMissingSelection', 'folderOcrLoadsOnSaveAndRoutesSharedWorker',
    'folderOcrValidationRejectsPackageMembershipChanges', 'customV6SchemaAndLocalIdsAreStrict'], info);
  const output = await workspace();
  expect(await native('TaskPinTranslationProbe', [output, '--folder-model'], output)).toBe(0);
  const report = JSON.parse(await fs.readFile(path.join(output, 'native-probe.json'), 'utf8'));
  expect(report.profileId).toMatch(/^local-[a-f0-9]{64}$/);
  expect(report.ocrEngine).toBe('PaddleOCR PP-OCRv6');
  expect(report.state).toBe('PRESENTING');
  expect(report.positionAndZoomPreserved).toBe(true);
  await info.attach('Dropped filename-only pair metadata', { path: path.join(output, 'native-probe.json'), contentType: 'application/json' });
});

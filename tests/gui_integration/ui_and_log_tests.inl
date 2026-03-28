void RunConfigErrorGuiTest(TestRunMode runMode = TestRunMode::Automated) {
    DummyWindow window(kWindowWidth, kWindowHeight, runMode == TestRunMode::Visual);
    const std::filesystem::path root = PrepareCaseDirectory("config_error_gui");
    ResetGlobalTestState(root);

    const std::filesystem::path configPath = root / "config.toml";
    std::ofstream out(configPath, std::ios::binary | std::ios::trunc);
    Expect(out.is_open(), "Failed to open config.toml for invalid-config test setup.");
    out << "configVersion = 4\n[[modes]]\nid = \"Fullscreen\"\nwidth =\n";
    out.close();

    LoadConfig();

    Expect(g_configLoadFailed.load(std::memory_order_acquire), "Expected invalid TOML to mark config loading as failed.");
    {
        std::lock_guard<std::mutex> lock(g_configErrorMutex);
        Expect(!g_configLoadError.empty(), "Expected invalid TOML to populate a config error message.");
    }

    if (runMode == TestRunMode::Visual) {
        RunVisualLoop(window, "config-error-gui", &RenderInteractiveConfigErrorFrame);
        return;
    }

    RenderConfigErrorFrame(window);
}

void RunSettingsGuiBasicTest(TestRunMode runMode = TestRunMode::Automated) {
    DummyWindow window(kWindowWidth, kWindowHeight, runMode == TestRunMode::Visual);
    const std::filesystem::path root = PrepareCaseDirectory("settings_gui_basic");
    ResetGlobalTestState(root);

    LoadConfig();
    Expect(!g_configLoadFailed.load(std::memory_order_acquire), "Expected config load to succeed before basic GUI rendering.");

    g_config.basicModeEnabled = true;
    g_configIsDirty.store(false, std::memory_order_release);

    if (runMode == TestRunMode::Visual) {
        RunVisualLoop(window, "settings-gui-basic", &RenderInteractiveSettingsFrame);
        return;
    }

    const std::vector<std::string> tabs = {
        tr("tabs.general"),
        tr("tabs.other"),
        tr("tabs.supporters"),
    };
    for (const std::string& tab : tabs) {
        RenderSettingsFrame(window, tab.c_str());
    }
}

void RunSettingsGuiAdvancedTest(TestRunMode runMode = TestRunMode::Automated) {
    DummyWindow window(kWindowWidth, kWindowHeight, runMode == TestRunMode::Visual);
    const std::filesystem::path root = PrepareCaseDirectory("settings_gui_advanced");
    ResetGlobalTestState(root);

    LoadConfig();
    Expect(!g_configLoadFailed.load(std::memory_order_acquire), "Expected config load to succeed before advanced GUI rendering.");

    g_config.basicModeEnabled = false;
    g_configIsDirty.store(false, std::memory_order_release);

    if (runMode == TestRunMode::Visual) {
        RunVisualLoop(window, "settings-gui-advanced", &RenderInteractiveSettingsFrame);
        return;
    }

    const std::string inputsTab = tr("tabs.inputs");
    const std::string mouseSubTab = tr("inputs.mouse");
    const std::string keyboardSubTab = tr("inputs.keyboard");

    const std::vector<std::string> tabs = {
        tr("tabs.modes"),
        tr("tabs.mirrors"),
        tr("tabs.images"),
        tr("tabs.window_overlays"),
        tr("tabs.browser_overlays"),
        tr("tabs.hotkeys"),
        inputsTab,
        tr("tabs.settings"),
        tr("tabs.appearance"),
        tr("tabs.misc"),
        tr("tabs.supporters"),
    };

    for (const std::string& tab : tabs) {
        if (tab == inputsTab) {
            RenderSettingsFrame(window, tab.c_str(), mouseSubTab.c_str());
            RenderSettingsFrame(window, tab.c_str(), keyboardSubTab.c_str());
            continue;
        }

        RenderSettingsFrame(window, tab.c_str());
    }
}

void RunSettingsTabGeneralPopulatedTest(TestRunMode runMode = TestRunMode::Automated) {
    RunPopulatedSettingsTabCase("settings_tab_general_populated", tr("tabs.general"), std::string(), runMode);
}

void RunSettingsTabOtherPopulatedTest(TestRunMode runMode = TestRunMode::Automated) {
    RunPopulatedSettingsTabCase("settings_tab_other_populated", tr("tabs.other"), std::string(), runMode);
}

void RunSettingsTabModesPopulatedTest(TestRunMode runMode = TestRunMode::Automated) {
    RunPopulatedSettingsTabCase("settings_tab_modes_populated", tr("tabs.modes"), std::string(), runMode);
}

void RunSettingsTabMirrorsPopulatedTest(TestRunMode runMode = TestRunMode::Automated) {
    RunPopulatedSettingsTabCase("settings_tab_mirrors_populated", tr("tabs.mirrors"), std::string(), runMode);
}

void RunSettingsTabImagesPopulatedTest(TestRunMode runMode = TestRunMode::Automated) {
    RunPopulatedSettingsTabCase("settings_tab_images_populated", tr("tabs.images"), std::string(), runMode);
}

void RunSettingsTabWindowOverlaysPopulatedTest(TestRunMode runMode = TestRunMode::Automated) {
    RunPopulatedSettingsTabCase("settings_tab_window_overlays_populated", tr("tabs.window_overlays"), std::string(), runMode);
}

void RunSettingsTabBrowserOverlaysPopulatedTest(TestRunMode runMode = TestRunMode::Automated) {
    RunPopulatedSettingsTabCase("settings_tab_browser_overlays_populated", tr("tabs.browser_overlays"), std::string(), runMode);
}

void RunSettingsTabHotkeysPopulatedTest(TestRunMode runMode = TestRunMode::Automated) {
    RunPopulatedSettingsTabCase("settings_tab_hotkeys_populated", tr("tabs.hotkeys"), std::string(), runMode);
}

void RunSettingsTabInputsMousePopulatedTest(TestRunMode runMode = TestRunMode::Automated) {
    RunPopulatedSettingsTabCase("settings_tab_inputs_mouse_populated", tr("tabs.inputs"), tr("inputs.mouse"), runMode);
}

void RunSettingsTabInputsKeyboardPopulatedTest(TestRunMode runMode = TestRunMode::Automated) {
    RunPopulatedSettingsTabCase("settings_tab_inputs_keyboard_populated", tr("tabs.inputs"), tr("inputs.keyboard"), runMode);
}

void RunSettingsTabSettingsPopulatedTest(TestRunMode runMode = TestRunMode::Automated) {
    RunPopulatedSettingsTabCase("settings_tab_settings_populated", tr("tabs.settings"), std::string(), runMode);
}

void RunSettingsTabAppearancePopulatedTest(TestRunMode runMode = TestRunMode::Automated) {
    RunPopulatedSettingsTabCase("settings_tab_appearance_populated", tr("tabs.appearance"), std::string(), runMode);
}

void RunSettingsTabMiscPopulatedTest(TestRunMode runMode = TestRunMode::Automated) {
    RunPopulatedSettingsTabCase("settings_tab_misc_populated", tr("tabs.misc"), std::string(), runMode);
}

void RunSettingsTabSupportersPopulatedTest(TestRunMode runMode = TestRunMode::Automated) {
    RunPopulatedSettingsTabCase("settings_tab_supporters_populated", tr("tabs.supporters"), std::string(), runMode);
}

void RunSettingsTabGeneralDefaultTest(TestRunMode runMode = TestRunMode::Automated) {
    RunDefaultSettingsTabCase("settings_tab_general_default", tr("tabs.general"), std::string(), true, runMode);
}

void RunSettingsTabOtherDefaultTest(TestRunMode runMode = TestRunMode::Automated) {
    RunDefaultSettingsTabCase("settings_tab_other_default", tr("tabs.other"), std::string(), true, runMode);
}

void RunSettingsTabSupportersDefaultTest(TestRunMode runMode = TestRunMode::Automated) {
    RunDefaultSettingsTabCase("settings_tab_supporters_default", tr("tabs.supporters"), std::string(), true, runMode);
}

void RunSettingsTabModesDefaultTest(TestRunMode runMode = TestRunMode::Automated) {
    RunDefaultSettingsTabCase("settings_tab_modes_default", tr("tabs.modes"), std::string(), false, runMode);
}

void RunSettingsTabMirrorsDefaultTest(TestRunMode runMode = TestRunMode::Automated) {
    RunDefaultSettingsTabCase("settings_tab_mirrors_default", tr("tabs.mirrors"), std::string(), false, runMode);
}

void RunSettingsTabImagesDefaultTest(TestRunMode runMode = TestRunMode::Automated) {
    RunDefaultSettingsTabCase("settings_tab_images_default", tr("tabs.images"), std::string(), false, runMode);
}

void RunSettingsTabWindowOverlaysDefaultTest(TestRunMode runMode = TestRunMode::Automated) {
    RunDefaultSettingsTabCase("settings_tab_window_overlays_default", tr("tabs.window_overlays"), std::string(), false, runMode);
}

void RunSettingsTabBrowserOverlaysDefaultTest(TestRunMode runMode = TestRunMode::Automated) {
    RunDefaultSettingsTabCase("settings_tab_browser_overlays_default", tr("tabs.browser_overlays"), std::string(), false,
                              runMode);
}

void RunSettingsTabHotkeysDefaultTest(TestRunMode runMode = TestRunMode::Automated) {
    RunDefaultSettingsTabCase("settings_tab_hotkeys_default", tr("tabs.hotkeys"), std::string(), false, runMode);
}

void RunSettingsTabInputsMouseDefaultTest(TestRunMode runMode = TestRunMode::Automated) {
    RunDefaultSettingsTabCase("settings_tab_inputs_mouse_default", tr("tabs.inputs"), tr("inputs.mouse"), false, runMode);
}

void RunSettingsTabInputsKeyboardDefaultTest(TestRunMode runMode = TestRunMode::Automated) {
    RunDefaultSettingsTabCase("settings_tab_inputs_keyboard_default", tr("tabs.inputs"), tr("inputs.keyboard"), false, runMode);
}

void RunSettingsTabSettingsDefaultTest(TestRunMode runMode = TestRunMode::Automated) {
    RunDefaultSettingsTabCase("settings_tab_settings_default", tr("tabs.settings"), std::string(), false, runMode);
}

void RunSettingsTabAppearanceDefaultTest(TestRunMode runMode = TestRunMode::Automated) {
    RunDefaultSettingsTabCase("settings_tab_appearance_default", tr("tabs.appearance"), std::string(), false, runMode);
}

void RunSettingsTabMiscDefaultTest(TestRunMode runMode = TestRunMode::Automated) {
    RunDefaultSettingsTabCase("settings_tab_misc_default", tr("tabs.misc"), std::string(), false, runMode);
}

void RunLogMultiInstanceLatestSuffixTest(TestRunMode runMode = TestRunMode::Automated) {
    (void)runMode;
    ProcessQueuedArchivedLogCompressions();

    const std::filesystem::path root = PrepareCaseDirectory("log_multi_instance_latest_suffix");
    ResetGlobalTestState(root);
    const std::filesystem::path logsDirectory = root / "logs";

    ScopedLogSessionClaim first(logsDirectory);
    ScopedLogSessionClaim second(logsDirectory);
    ScopedLogSessionClaim third(logsDirectory);

    Expect(std::filesystem::path(first.session().logFilePath).filename() == "latest.log",
           "Expected the first claimed session to use latest.log.");
    Expect(std::filesystem::path(second.session().logFilePath).filename() == "latest-1.log",
           "Expected the second claimed session to use latest-1.log.");
    Expect(std::filesystem::path(third.session().logFilePath).filename() == "latest-2.log",
           "Expected the third claimed session to use latest-2.log.");

    Expect(first.session().otherOpenInstances.empty(),
           "Expected the first claimed session to observe no earlier open instances.");
    Expect(second.session().otherOpenInstances.size() == 1,
           "Expected the second claimed session to observe the first open instance.");
    Expect(third.session().otherOpenInstances.size() == 2,
           "Expected the third claimed session to observe the two earlier open instances.");

    std::vector<std::string> thirdPeerNames;
    for (const LogInstanceInfo& peer : third.session().otherOpenInstances) {
        thirdPeerNames.push_back(std::filesystem::path(peer.logFilePath).filename().string());
    }
    std::sort(thirdPeerNames.begin(), thirdPeerNames.end());
    ExpectVectorEquals(thirdPeerNames, std::vector<std::string>{ "latest-1.log", "latest.log" },
                       "Expected the third claimed session to list both lower-numbered latest logs.");
}

void RunLogHeaderIncludesPeerInfoTest(TestRunMode runMode = TestRunMode::Automated) {
    (void)runMode;
    ProcessQueuedArchivedLogCompressions();

    const std::filesystem::path root = PrepareCaseDirectory("log_header_includes_peer_info");
    ResetGlobalTestState(root);
    const std::filesystem::path logsDirectory = root / "logs";

    ScopedLogSessionClaim first(logsDirectory);
    WriteTextFileUtf8(std::filesystem::path(first.session().logFilePath), BuildLogSessionHeader(first.session()));

    ScopedLogSessionClaim second(logsDirectory);
    WriteTextFileUtf8(std::filesystem::path(second.session().logFilePath), BuildLogSessionHeader(second.session()));

    const std::string firstHeader = ReadTextFileUtf8(std::filesystem::path(first.session().logFilePath));
    const std::string secondHeader = ReadTextFileUtf8(std::filesystem::path(second.session().logFilePath));

    Expect(firstHeader.find("Other open instances at startup: 0") != std::string::npos,
           "Expected the first log header to report zero other open instances.");
    Expect(secondHeader.find("Other open instances at startup: 1") != std::string::npos,
           "Expected the second log header to report the earlier open instance.");
    Expect(secondHeader.find("latest.log") != std::string::npos,
           "Expected the second log header to include the other instance's latest.log path.");
    Expect(secondHeader.find("Process ID: " + std::to_string(second.session().processId)) != std::string::npos,
           "Expected the log header to include the current process ID.");
}

void RunLogArchiveCollisionHandlingTest(TestRunMode runMode = TestRunMode::Automated) {
    (void)runMode;
    ProcessQueuedArchivedLogCompressions();

    const std::filesystem::path root = PrepareCaseDirectory("log_archive_collision_handling");
    ResetGlobalTestState(root);
    const std::filesystem::path logsDirectory = root / "logs";
    std::error_code error;
    std::filesystem::create_directories(logsDirectory, error);
    Expect(!error, "Failed to create log fixture directory.");

    const std::filesystem::path orphanLatest = logsDirectory / "latest.log";
    WriteTextFileUtf8(orphanLatest, "orphan latest log");
    SetLocalFileLastWriteTime(orphanLatest, 2024, 1, 2, 3, 4, 5);

    const std::filesystem::path staleOwnedLatest = logsDirectory / "latest-1.log";
    WriteTextFileUtf8(staleOwnedLatest, "stale owned latest log");
    SetLocalFileLastWriteTime(staleOwnedLatest, 2024, 1, 2, 3, 4, 5);

    WriteTextFileUtf8(logsDirectory / "latest-1.log.owner",
                      "pid=999999\nprocessStartFileTime=1\nlogFile=" + Narrow(staleOwnedLatest.wstring()) + "\n");

    const std::filesystem::path existingArchive0 = logsDirectory / "20240102_030405.log.gz";
    const std::filesystem::path existingArchive1 = logsDirectory / "20240102_030405_1.log.gz";
    WriteTextFileUtf8(existingArchive0, "existing-archive-zero");
    WriteTextFileUtf8(existingArchive1, "existing-archive-one");

    ScopedLogSessionClaim claim(logsDirectory);
    ProcessQueuedArchivedLogCompressions();

    const std::vector<std::string> fileNames = ListDirectoryFileNamesSorted(logsDirectory);
    Expect(ContainsFileName(fileNames, "20240102_030405.log.gz"),
           "Expected the existing base archive gzip to remain present.");
    Expect(ContainsFileName(fileNames, "20240102_030405_1.log.gz"),
           "Expected the existing suffixed archive gzip to remain present.");
    Expect(ContainsFileName(fileNames, "20240102_030405_2.log.gz"),
           "Expected the first archived latest log to move to the next free gzip suffix.");
    Expect(ContainsFileName(fileNames, "20240102_030405_3.log.gz"),
           "Expected the second archived latest log to avoid both existing and newly queued names.");

    Expect(ReadTextFileUtf8(existingArchive0) == "existing-archive-zero",
           "Expected archive collision handling to avoid overwriting the existing base gzip.");
    Expect(ReadTextFileUtf8(existingArchive1) == "existing-archive-one",
           "Expected archive collision handling to avoid overwriting the existing suffixed gzip.");
    Expect(std::filesystem::path(claim.session().logFilePath).filename() == "latest.log",
           "Expected a new active claim to still reuse latest.log after archiving stale files.");
}

void RunLogReleasedSlotReuseTest(TestRunMode runMode = TestRunMode::Automated) {
    (void)runMode;
    ProcessQueuedArchivedLogCompressions();

    const std::filesystem::path root = PrepareCaseDirectory("log_released_slot_reuse");
    ResetGlobalTestState(root);
    const std::filesystem::path logsDirectory = root / "logs";

    ScopedLogSessionClaim first(logsDirectory);
    WriteTextFileUtf8(std::filesystem::path(first.session().logFilePath), "released instance log contents");
    SetLocalFileLastWriteTime(std::filesystem::path(first.session().logFilePath), 2024, 1, 3, 4, 5, 6);

    ScopedLogSessionClaim second(logsDirectory);
    WriteTextFileUtf8(std::filesystem::path(second.session().logFilePath), "still-active instance log contents");

    first.Release();

    ScopedLogSessionClaim third(logsDirectory);
    ProcessQueuedArchivedLogCompressions();

    Expect(std::filesystem::path(third.session().logFilePath).filename() == "latest.log",
           "Expected the next claimed session to reuse latest.log after the old owner released it.");
    Expect(third.session().otherOpenInstances.size() == 1,
           "Expected the reused latest.log claim to still observe the other active instance.");
    Expect(std::filesystem::path(third.session().otherOpenInstances.front().logFilePath).filename() == "latest-1.log",
           "Expected the surviving active instance to remain on latest-1.log.");

    const std::vector<std::string> fileNames = ListDirectoryFileNamesSorted(logsDirectory);
    Expect(ContainsFileName(fileNames, "20240103_040506.log.gz"),
           "Expected the released latest.log file to be archived and compressed before reuse.");
    Expect(ContainsFileName(fileNames, "latest-1.log.owner"),
           "Expected the other active instance to retain its owner file.");
    Expect(!ContainsFileName(fileNames, "latest-2.log.owner"),
           "Expected slot reuse to avoid allocating a higher-numbered latest log unnecessarily.");
}
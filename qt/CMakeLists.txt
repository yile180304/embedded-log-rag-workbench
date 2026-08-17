cmake_minimum_required(VERSION 3.21)

project(RagDiagnosticWorkbench VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets Sql)

qt_add_executable(RagDiagnosticWorkbench
    src/main.cpp
    src/diagnosticworkbench.cpp
    src/diagnosticworkbench.h
    src/diagnosticworkorder.cpp
    src/diagnosticworkorder.h
    src/diagnosisviewmodel.cpp
    src/diagnosisviewmodel.h
    src/cases/caserecord.cpp
    src/cases/caserecord.h
    src/cases/casestore.cpp
    src/cases/casestore.h
    src/evaluation/evaluationrecord.cpp
    src/evaluation/evaluationrecord.h
    src/evaluation/evaluationstore.cpp
    src/evaluation/evaluationstore.h
    src/storage/localdatabaseschema.cpp
    src/storage/localdatabaseschema.h
    src/knowledge/knowledgesourcerecord.cpp
    src/knowledge/knowledgesourcerecord.h
    src/protocol/protocollogviewmodel.cpp
    src/protocol/protocollogviewmodel.h
    src/protocol/protocollogpanel.cpp
    src/protocol/protocollogpanel.h
    src/protocol/protocolworkorder.cpp
    src/protocol/protocolworkorder.h
    src/runtime/ragprocessbridge.cpp
    src/runtime/ragprocessbridge.h
    src/runtime/runtimesettings.cpp
    src/runtime/runtimesettings.h
    src/runtime/runtimesettingsdialog.cpp
    src/runtime/runtimesettingsdialog.h
    src/workspace/diagnosisworkspacepage.cpp
    src/workspace/diagnosisworkspacepage.h
    src/workspace/caselibrarypage.cpp
    src/workspace/caselibrarypage.h
    src/workspace/evaluationcenterpage.cpp
    src/workspace/evaluationcenterpage.h
    src/workspace/knowledgecenterpage.cpp
    src/workspace/knowledgecenterpage.h
    src/workspace/workspacetaskstate.h
)

target_link_libraries(RagDiagnosticWorkbench PRIVATE Qt6::Widgets Qt6::Sql)
target_include_directories(RagDiagnosticWorkbench PRIVATE src)

set_target_properties(RagDiagnosticWorkbench PROPERTIES
    WIN32_EXECUTABLE TRUE
)

add_executable(RagProcessBridgeSmoke
    tests/ragprocessbridge_smoke.cpp
    src/diagnosisviewmodel.cpp
    src/diagnosisviewmodel.h
    src/protocol/protocollogviewmodel.cpp
    src/protocol/protocollogviewmodel.h
    src/runtime/ragprocessbridge.cpp
    src/runtime/ragprocessbridge.h
)
target_link_libraries(RagProcessBridgeSmoke PRIVATE Qt6::Core)
target_include_directories(RagProcessBridgeSmoke PRIVATE src)

add_executable(RuntimeSettingsSmoke
    tests/runtimesettings_smoke.cpp
    src/runtime/runtimesettings.cpp
    src/runtime/runtimesettings.h
    src/runtime/ragprocessbridge.cpp
    src/runtime/ragprocessbridge.h
)
target_link_libraries(RuntimeSettingsSmoke PRIVATE Qt6::Core)
target_include_directories(RuntimeSettingsSmoke PRIVATE src)

add_executable(RuntimeSettingsDialogSmoke
    tests/runtimesettingsdialog_smoke.cpp
    src/runtime/runtimesettingsdialog.cpp
    src/runtime/runtimesettingsdialog.h
    src/runtime/runtimesettings.cpp
    src/runtime/runtimesettings.h
    src/runtime/ragprocessbridge.cpp
    src/runtime/ragprocessbridge.h
)
target_link_libraries(RuntimeSettingsDialogSmoke PRIVATE Qt6::Widgets)
target_include_directories(RuntimeSettingsDialogSmoke PRIVATE src)

add_executable(DiagnosisWorkspacePageSmoke
    tests/diagnosisworkspacepage_smoke.cpp
    src/workspace/diagnosisworkspacepage.cpp
    src/workspace/diagnosisworkspacepage.h
    src/workspace/workspacetaskstate.h
    src/diagnosticworkorder.cpp
    src/diagnosticworkorder.h
    src/diagnosisviewmodel.cpp
    src/diagnosisviewmodel.h
    src/protocol/protocollogviewmodel.cpp
    src/protocol/protocollogviewmodel.h
    src/protocol/protocollogpanel.cpp
    src/protocol/protocollogpanel.h
    src/protocol/protocolworkorder.cpp
    src/protocol/protocolworkorder.h
    src/runtime/ragprocessbridge.cpp
    src/runtime/ragprocessbridge.h
)
target_link_libraries(DiagnosisWorkspacePageSmoke PRIVATE Qt6::Widgets)
target_include_directories(DiagnosisWorkspacePageSmoke PRIVATE src)

add_executable(CaseStoreSmoke
    tests/casestore_smoke.cpp
    src/cases/caserecord.cpp
    src/cases/caserecord.h
    src/cases/casestore.cpp
    src/cases/casestore.h
    src/storage/localdatabaseschema.cpp
    src/storage/localdatabaseschema.h
)
target_link_libraries(CaseStoreSmoke PRIVATE Qt6::Core Qt6::Sql)
target_include_directories(CaseStoreSmoke PRIVATE src)

add_executable(CaseLibraryPageSmoke
    tests/caselibrarypage_smoke.cpp
    src/workspace/caselibrarypage.cpp
    src/workspace/caselibrarypage.h
)
target_link_libraries(CaseLibraryPageSmoke PRIVATE Qt6::Widgets)
target_include_directories(CaseLibraryPageSmoke PRIVATE src)

add_executable(KnowledgeCenterPageSmoke
    tests/knowledgecenterpage_smoke.cpp
    src/knowledge/knowledgesourcerecord.cpp
    src/knowledge/knowledgesourcerecord.h
    src/workspace/knowledgecenterpage.cpp
    src/workspace/knowledgecenterpage.h
)
target_link_libraries(KnowledgeCenterPageSmoke PRIVATE Qt6::Widgets)
target_include_directories(KnowledgeCenterPageSmoke PRIVATE src)

add_executable(WorkbenchCaseLibrarySmoke
    tests/workbench_case_library_smoke.cpp
    src/diagnosticworkbench.cpp
    src/diagnosticworkbench.h
    src/diagnosticworkorder.cpp
    src/diagnosticworkorder.h
    src/diagnosisviewmodel.cpp
    src/diagnosisviewmodel.h
    src/cases/caserecord.cpp
    src/cases/caserecord.h
    src/cases/casestore.cpp
    src/cases/casestore.h
    src/evaluation/evaluationrecord.cpp
    src/evaluation/evaluationrecord.h
    src/evaluation/evaluationstore.cpp
    src/evaluation/evaluationstore.h
    src/storage/localdatabaseschema.cpp
    src/storage/localdatabaseschema.h
    src/knowledge/knowledgesourcerecord.cpp
    src/knowledge/knowledgesourcerecord.h
    src/protocol/protocollogviewmodel.cpp
    src/protocol/protocollogviewmodel.h
    src/protocol/protocollogpanel.cpp
    src/protocol/protocollogpanel.h
    src/protocol/protocolworkorder.cpp
    src/protocol/protocolworkorder.h
    src/runtime/ragprocessbridge.cpp
    src/runtime/ragprocessbridge.h
    src/runtime/runtimesettings.cpp
    src/runtime/runtimesettings.h
    src/runtime/runtimesettingsdialog.cpp
    src/runtime/runtimesettingsdialog.h
    src/workspace/diagnosisworkspacepage.cpp
    src/workspace/diagnosisworkspacepage.h
    src/workspace/caselibrarypage.cpp
    src/workspace/caselibrarypage.h
    src/workspace/evaluationcenterpage.cpp
    src/workspace/evaluationcenterpage.h
    src/workspace/knowledgecenterpage.cpp
    src/workspace/knowledgecenterpage.h
    src/workspace/workspacetaskstate.h
)
target_link_libraries(WorkbenchCaseLibrarySmoke PRIVATE Qt6::Widgets Qt6::Sql)
target_include_directories(WorkbenchCaseLibrarySmoke PRIVATE src)

add_executable(WorkbenchKnowledgeSmoke
    tests/workbench_knowledge_smoke.cpp
    src/diagnosticworkbench.cpp
    src/diagnosticworkbench.h
    src/diagnosticworkorder.cpp
    src/diagnosticworkorder.h
    src/diagnosisviewmodel.cpp
    src/diagnosisviewmodel.h
    src/cases/caserecord.cpp
    src/cases/caserecord.h
    src/cases/casestore.cpp
    src/cases/casestore.h
    src/evaluation/evaluationrecord.cpp
    src/evaluation/evaluationrecord.h
    src/evaluation/evaluationstore.cpp
    src/evaluation/evaluationstore.h
    src/storage/localdatabaseschema.cpp
    src/storage/localdatabaseschema.h
    src/knowledge/knowledgesourcerecord.cpp
    src/knowledge/knowledgesourcerecord.h
    src/protocol/protocollogviewmodel.cpp
    src/protocol/protocollogviewmodel.h
    src/protocol/protocollogpanel.cpp
    src/protocol/protocollogpanel.h
    src/protocol/protocolworkorder.cpp
    src/protocol/protocolworkorder.h
    src/runtime/ragprocessbridge.cpp
    src/runtime/ragprocessbridge.h
    src/runtime/runtimesettings.cpp
    src/runtime/runtimesettings.h
    src/runtime/runtimesettingsdialog.cpp
    src/runtime/runtimesettingsdialog.h
    src/workspace/diagnosisworkspacepage.cpp
    src/workspace/diagnosisworkspacepage.h
    src/workspace/caselibrarypage.cpp
    src/workspace/caselibrarypage.h
    src/workspace/evaluationcenterpage.cpp
    src/workspace/evaluationcenterpage.h
    src/workspace/knowledgecenterpage.cpp
    src/workspace/knowledgecenterpage.h
    src/workspace/workspacetaskstate.h
)
target_link_libraries(WorkbenchKnowledgeSmoke PRIVATE Qt6::Widgets Qt6::Sql)
target_include_directories(WorkbenchKnowledgeSmoke PRIVATE src)

add_executable(WorkbenchEvaluationSmoke
    tests/workbench_evaluation_smoke.cpp
    src/diagnosticworkbench.cpp
    src/diagnosticworkbench.h
    src/diagnosticworkorder.cpp
    src/diagnosticworkorder.h
    src/diagnosisviewmodel.cpp
    src/diagnosisviewmodel.h
    src/cases/caserecord.cpp
    src/cases/caserecord.h
    src/cases/casestore.cpp
    src/cases/casestore.h
    src/evaluation/evaluationrecord.cpp
    src/evaluation/evaluationrecord.h
    src/evaluation/evaluationstore.cpp
    src/evaluation/evaluationstore.h
    src/storage/localdatabaseschema.cpp
    src/storage/localdatabaseschema.h
    src/knowledge/knowledgesourcerecord.cpp
    src/knowledge/knowledgesourcerecord.h
    src/protocol/protocollogviewmodel.cpp
    src/protocol/protocollogviewmodel.h
    src/protocol/protocollogpanel.cpp
    src/protocol/protocollogpanel.h
    src/protocol/protocolworkorder.cpp
    src/protocol/protocolworkorder.h
    src/runtime/ragprocessbridge.cpp
    src/runtime/ragprocessbridge.h
    src/runtime/runtimesettings.cpp
    src/runtime/runtimesettings.h
    src/runtime/runtimesettingsdialog.cpp
    src/runtime/runtimesettingsdialog.h
    src/workspace/diagnosisworkspacepage.cpp
    src/workspace/diagnosisworkspacepage.h
    src/workspace/caselibrarypage.cpp
    src/workspace/caselibrarypage.h
    src/workspace/evaluationcenterpage.cpp
    src/workspace/evaluationcenterpage.h
    src/workspace/knowledgecenterpage.cpp
    src/workspace/knowledgecenterpage.h
    src/workspace/workspacetaskstate.h
)
target_link_libraries(WorkbenchEvaluationSmoke PRIVATE Qt6::Widgets Qt6::Sql)
target_include_directories(WorkbenchEvaluationSmoke PRIVATE src)

add_executable(EvaluationCenterPageSmoke
    tests/evaluationcenterpage_smoke.cpp
    src/workspace/evaluationcenterpage.cpp
    src/workspace/evaluationcenterpage.h
    src/workspace/workspacetaskstate.h
    src/runtime/ragprocessbridge.h
)
target_link_libraries(EvaluationCenterPageSmoke PRIVATE Qt6::Widgets Qt6::Core)
target_include_directories(EvaluationCenterPageSmoke PRIVATE src)

add_executable(EvaluationStoreSmoke
    tests/evaluationstore_smoke.cpp
    src/evaluation/evaluationrecord.cpp
    src/evaluation/evaluationrecord.h
    src/evaluation/evaluationstore.cpp
    src/evaluation/evaluationstore.h
    src/storage/localdatabaseschema.cpp
    src/storage/localdatabaseschema.h
)
target_link_libraries(EvaluationStoreSmoke PRIVATE Qt6::Core Qt6::Sql)
target_include_directories(EvaluationStoreSmoke PRIVATE src)

# 네이티브 에디터 플러그인 예제

이 예제는 엔진과 별도로 빌드하는 `.dll` / `.dylib`입니다. ImGui, C++ 객체, GPU 포인터를 ABI로 넘기지 않고 `jEditorExtensions.h`의 버전 1 C API만 사용합니다. 엔진의 Extensions 창에서 라이브러리 경로를 넣고 **Load extension**을 누르세요.

```sh
cmake -S examples/editor-plugin -B build/editor-plugin -DCMAKE_BUILD_TYPE=Debug
cmake --build build/editor-plugin --config Debug
```

macOS 출력은 보통 `build/editor-plugin/jengine_example_plugin.dylib`, Windows 다중 구성 출력은 `build/editor-plugin/Debug/jengine_example_plugin.dll`입니다. 실제 출력 폴더의 파일을 선택하세요. 예제 UI의 **Create cube**로 실제 문서 엔티티를 만들고 **Cube position**으로 위치를 바꿀 수 있습니다.

플러그인 원본은 직접 로드하지 않고 호스트의 임시 폴더에 복사해 로드합니다. 빌드 결과를 다시 쓰는 동안 기존 버전은 계속 실행됩니다. **Watch and reload**를 켜면 변경된 파일의 수정 시각과 크기가 0.5초 이상 안정된 뒤 새 버전을 시도합니다. 수동 **Reload**, **Unload**도 가능합니다. 라이브러리를 자동으로 컴파일하거나 외부 서비스를 설치하지 않습니다.

교체 라이브러리의 ABI·필수 함수·상태 복원이 실패하면 이전 버전을 유지합니다. `SaveState` / `RestoreState`는 선택적인 쌍이며 상태는 최대 64 KiB입니다. 상태 데이터에는 자신의 스키마 버전을 넣어 호환성을 확인하세요. 예제는 카운터와 엔티티 ID, 위치를 보존합니다. 스냅샷 버퍼의 capacity를 넘겨 쓰면 안 됩니다.

모든 콜백과 호스트 함수는 에디터 소유 스레드에서만 사용하세요. `Text`, `Button`, `DragFloat3`는 `Draw` 콜백 안에서만 유효합니다. 백그라운드 콜백, 자체 GPU 스레드, 콜백 종료 후 UI 포인터 사용은 지원하지 않습니다. `OnUnload`에서는 자신의 상태와 자원만 정리하고 종료해야 합니다. 생성한 엔티티는 플러그인 코드 포인터를 보유하지 않는 일반 문서 데이터이며 플러그인 언로드 후에도 남습니다.

저장한 엔티티 ID는 현재 문서에 속합니다. 호스트 통합은 새 장면·프로젝트를 열기 전에 `Shutdown(editor)`를 호출해야 합니다. UI에서 다른 문서로 바꾸기 전에는 플러그인을 언로드하세요. 같은 문서 안의 코드 재로드에서 상태와 ID를 이어가며, 새 문서에서는 다시 로드하고 사용할 물체를 다시 만듭니다.

네이티브 라이브러리는 엔진 프로세스에서 실행되므로 신뢰하는 코드만 로드하세요. 잘못된 네이티브 메모리 접근을 격리하는 샌드박스는 아닙니다. 외부 동적 의존성이 있는 플러그인은 해당 의존성을 운영체제가 정상적으로 찾을 수 있도록 별도로 배포해야 합니다. 이 예제 자체는 플랫폼 C++ 런타임 외의 동적 의존성이 없습니다.

실제 라이브러리 기반 회귀 검사용 변형도 빌드할 수 있습니다.

```sh
cmake -S examples/editor-plugin -B build/editor-plugin-fixtures \
  -DCMAKE_BUILD_TYPE=Debug -DJENGINE_BUILD_PLUGIN_FIXTURES=ON
cmake --build build/editor-plugin-fixtures --config Debug
```

`jengine_plugin_fixture_v1/v2`, `badabi`, `failload`, `failrestore`, `oversized`, `missingexport`가 생성됩니다. 엔진 CMake 테스트에서는 `JENGINE_PLUGIN_FIXTURE_DIRECTORY`를 이 라이브러리 폴더로 정의하고 해당 타깃들을 먼저 빌드합니다. 테스트는 실제 로드·상태 보존·실패한 교체·소유 스레드·수정 시각 감시를 검사합니다. 테스트 빌드 도구는 엔진의 런타임 플러그인 로더와 분리되어 있습니다.

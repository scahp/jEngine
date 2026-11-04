# 스크린샷 제작 기록

이 폴더의 그림은 실제 jEngine 실행 화면에 빨간 네모와 번호를 추가한 사용설명서 자료입니다. `01`~`05`는 클래식 렌더링 패널, `06`~`08`은 새 기본 저작 화면입니다. 주석 없는 실행 원본은 [raw](raw/) 폴더에 있습니다. 화면의 성능 숫자는 촬영 순간의 값입니다.

## 기본 저작 화면 06~08

| 그림 | 번호 대상 | 변경하지 않은 실행 원본 |
| --- | --- | --- |
| [06-workspace.png](06-workspace.png) | 1 메뉴·Play, 2 Hierarchy, 3 Scene, 4 Inspector, 5 Project tools | [원본](raw/06-workspace.png) |
| [07-authoring-tools.png](07-authoring-tools.png) | 1 탭, 2 New node·Add·Undo·Redo, 3 캔버스·핀, 4 출력·Graph asset, 5 Save and Apply | [원본](raw/07-authoring-tools.png) |
| [08-project-settings.png](08-project-settings.png) | 1 Startup scene, 2 Renderer defaults, 3 Editor camera keys, 4 Save and Apply | [원본](raw/08-project-settings.png) |

- 실제 `examples/authoring-demo`를 바이트가 같은 격리 사본으로 복사한 뒤 정상 `--project` 시작 경로로 실행했습니다. 모형 UI나 새 컨트롤을 그리지 않았습니다.
- 각 실행은 120프레임 정상 종료, 60번째 GPU 프레임 캡처, 원본 1280×900, 전역 글자 배율 1.15입니다. Vulkan·Metal 검증 오류 0건이며 원본 프로젝트 11개 파일과 사용자 INI·`.jeditor` 설정은 보존됐습니다. 게시 전 현재 예제 해시와 다시 대조했습니다.
- 원본은 위 raw 파일로 그대로 복사했습니다. 주석은 **내장 `image_gen`의 참조 이미지 편집**만 사용했습니다. CLI/API 대체 경로, Python 래스터 편집이나 합성은 사용하지 않았습니다.
- 내장 도구가 주석본을 1495×1052로 확대해 반환했습니다. 원본과 UI 라벨·수치·상태·컨트롤·3D 장면을 시각 대조했으며, 생성된 확대본의 픽셀이 원본과 동일하다고 주장하지 않습니다. 정확한 실행 픽셀은 raw 파일과 SHA256을 기준으로 합니다.
- 전체 화면은 패널 제목까지 상자에 포함하도록, 도구 화면은 New node 선택칸까지 ② 상자에 포함하도록 각각 주석만 한 차례 보정했습니다. 새 글자는 흰 숫자뿐이며 그림마다 번호를 다시 시작합니다.
- 공통 요청은 실제 스크린샷을 고정 캔버스로 취급하고 UI·문구·값·상태·장면·구조를 유지한 채 빨간 테두리와 흰 숫자의 빨간 표식만 추가하는 것입니다. **실제 전달한 프롬프트 전체와 보정 프롬프트**는 [annotation-prompts.json](annotation-prompts.json)에 있습니다.
- 원본·주석본 크기/바이트/SHA256, 번호 대응, 소스 11개 해시, 실행 환경과 바이너리 출처는 [capture-info.json](capture-info.json)의 `authoring_capture_set` 및 `authoring-06-08` 항목에 기록했습니다. 로컬 전체 실행 기록은 `build/authoring/manual-release/`에 남깁니다.

## 클래식 화면 01~05

- 캡처 기준: 엔진 커밋 `0310df58`, Apple M4, macOS, Vulkan과 검증된 실험용 MoltenVK 런타임.
- 실행 원본: 1280×900, 화면별 120프레임 정상 종료, Vulkan·Metal 검증 오류 0건.
- 주석 제작: 내장 `imagegen` 편집 도구. 주석본은 1495×1052이며 원본과 라벨·버튼·순서를 대조했습니다.
- 공통 프롬프트: 원본 UI·문구·버튼·3D 장면을 보존하고 빨간 사각형과 흰 숫자가 든 빨간 번호만 추가. 새 컨트롤이나 설명 문구는 추가하지 않음.
- 화면별 번호 대상과 파일 해시: [capture-info.json](capture-info.json).

촬영을 위해 임시 실행 드라이버에서 기존 패널을 선택하고 큐브를 추가했습니다. 실제 엔진 소스는 변경하지 않았으며 촬영 후 로컬 UI 설정을 복구했습니다. 주석본의 화면 내용 확인이 필요하면 원본을 기준으로 보세요.

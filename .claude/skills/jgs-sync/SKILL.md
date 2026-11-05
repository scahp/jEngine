---
name: jgs-sync
description: JaehoGameStudio의 Claude Code·Codex 에이전트와 스킬을 양방향 동기화한다. 어느 쪽에서든 역할·스킬을 추가하거나 수정할 때 사용한다.
---

# Claude Code와 Codex 미러링

JaehoGameStudio의 에이전트 또는 스킬을 만들거나 수정하면 반대 플랫폼에도
반영하고 동기화 검사를 통과한 뒤 완료를 보고한다.

1. 프로젝트의 `.jgs-mirror.json`에 있는 `package` 상대 경로로 원본 패키지를
   찾는다. 패키지에서 직접 작업 중이면 `team.json`과 `scripts/sync-studio.py`가
   있는 루트를 사용한다. 설치 캐시나 사용자 전역 폴더를 원본으로 사용하지 않는다.
2. Python 3.11 이상으로 원본 패키지의 아래 명령을 실행한다. Windows에서도
   같은 스크립트를 사용하며 `py -3.11` 등 사용 가능한 Python을 선택한다.

   ```sh
   python3 <package>/scripts/sync-studio.py --project <engine>
   python3 <package>/scripts/sync-studio.py --project <engine> --check
   ```

   패키지만 수정하는 작업이면 `--project <engine>`을 생략한다.
3. Claude `agents/jgs-*.md` 또는 프로젝트 `.claude/agents/`에서 만든 역할은
   Codex `.codex/agents/`와 공통 `roles/`·`team.json`에도 반영한다. Codex에서
   시작한 변경도 같은 방식으로 Claude에 반영한다. 새 역할 ID는 `jgs-`로 시작한다.
4. 스킬은 공통 `skills/<name>/`와 프로젝트 `.claude/skills/<name>/`,
   `.agents/skills/<name>/` 사이에 전체 폴더를 동기화한다. `SKILL.md` 외에
   `scripts/`, `references/`, `assets/`도 포함한다. 새 스킬은 양쪽에서 발견된다.
5. 양쪽의 서로 다른 수정은 덮어쓰지 않는다. 충돌 내용을 검토해 하나의 의도된
   결과로 합친 후 재실행한다. 삭제·이름 변경은 자동 전파하지 않으며 별도로
   각 사본과 상태 기록을 함께 정리한다. 모델·권한 같은 플랫폼별 설정을
   역할 파일에 추가하지 않는다.

실제 엔진 기록은 해당 프로젝트 `.jaeho-studio/`에 유지한다. Claude·Codex·OpenClaw의
대화 이력은 공유되지 않으므로 기존 역할 체크포인트로 인계한다.
동기화 성공과 실행 중인 클라이언트의 새 정의 로딩은 구분한다. 새 Codex
에이전트는 새 대화에서 확인하고 Claude 플러그인은 필요한 경우 다시 로드한다.

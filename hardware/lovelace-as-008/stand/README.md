# LOVELACE AS-008 PCB 스탠드

[STL](lovelace-as-008-stand.stl)을 슬라이서에 불러오고 평평한 바닥을 출력판에 둡니다. [STEP](lovelace-as-008-stand.step)은 CAD 편집용입니다. 모델 단위는 mm입니다.

- 상판 경사 10°, 바깥쪽 낮은 끝 5.5 mm, 높은 끝 13.2 mm.
- 37 × 41 mm PCB용 안착 홈 37.5 × 41.5 mm. 홈 둘레 네 변에 높이 0.5 mm 턱이 이어지며 한쪽당 0.25 mm 여유가 있습니다.
- M2 파일럿 홀 2개, Ø1.75 × 3.8 mm. PCB를 180° 돌려 놓으면 H1이 반대 대각선 홀에 맞습니다. 사용할 홀에 M2 탭을 내고 M2 × 5 mm 나일론 나사 한 개로 고정합니다.

치수를 조정하려면 [build_stand.py](build_stand.py)를 편집한 뒤 이 폴더에서 아래 명령을 실행합니다. PCB 렌더링에 쓰는 `cadquery-ocp` 환경을 재사용합니다.

```sh
../rendering/.venv/bin/python build_stand.py
```

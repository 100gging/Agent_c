# 📄 PRD.md – Agent C (v1.0)

## 1. Overview

### 1.1 Project Name  
Agent C

### 1.2 Description  
Agent C는 라즈베리파이 기반 임베디드 시스템에서 동작하는 슈팅 게임으로,  
자이로 센서 및 터치 입력을 활용하여 에임을 조작하고 화면 상의 적을 맞추는 게임이다.  

초기 개발 단계에서는 터치 UI를 이용하여 조작을 구현하고,  
이후 자이로 센서 및 물리 버튼 입력으로 확장한다.

---

## 2. Goals

### 2.1 Primary Goal
- 임베디드 환경에서 동작하는 완성된 게임 루프 구현
- 직관적인 에임 조작 시스템 구현
- 센서 기반 확장 가능한 구조 설계

### 2.2 Success Criteria
- 게임 시작 → 플레이 → 종료 → 재시작 흐름이 자연스럽게 동작
- 사용자 입력에 대한 반응이 즉각적
- 최소 30FPS 이상 유지
- 발표 시 별도 설명 없이 이해 가능

---

## 3. Target Platform

- Raspberry Pi 3  
- Input: 터치 UI (초기), Gyroscope 및 버튼 (확장)  
- Display: HDMI Monitor  
- Framework: Qt (C++)

---

## 4. Core Game Loop

Main Menu → Calibration → Playing → Result → Restart

---

## 5. Game Design

### 5.1 Game Mode
- 제한 시간 60초 동안 점수 획득

### 5.2 Core Mechanics
- 에임 이동
- 발사
- 적 명중 시 점수 증가
- 시간 종료 시 게임 종료

---

## 6. User Flow

### Main Menu
- 게임 제목
- Start 버튼

### Calibration
- 정중앙 타겟(사과 이미지) 조준 후 발사
- 발사 시 해당 기준으로 정중앙 에임 기준값 설정
- 아래 게임 시작 버튼을 에임 발사를 통해 진행

### Playing
- 에임 이동
- 적 생성
- 발사 및 명중 판정
- 점수 및 시간 표시

### Result
- 점수 표시
- Retry / Main Menu

---

## 7. Functional Requirements

### Input
- 터치 버튼 (Up/Down/Left/Right, Fire, Calibrate)
- 이후 자이로 센서와 스위치 버튼을 활용하여 총의 움직임과 발사 버튼을 구현해야함

### Aim
- 좌표 기반 이동
- 화면 경계 제한

### Enemy
- 랜덤 위치 생성
- 명중 시 재생성

### Hit Detection
- 거리 기반 판정

### Game
- 60초 타이머
- 점수 시스템

### UI
- Main / Calibration / Game / Result 화면

### Feedback
- 발사 및 명중 효과

---

## 8. Non-Functional Requirements

- 30FPS 이상 유지
- 입력 지연 최소화
- 안정적 실행

---

## 9. Out of Scope

- 멀티플레이
- 카메라 기능
- 고급 UI 애니메이션

---

## 10. Future Work

- 자이로 입력
- 난이도 조절
- 사운드 설정

---

## Summary

Agent C는 터치 및 센서 입력을 기반으로 하는 임베디드 슈팅 게임이다.

// src/store/sensors.ts

import type { StateCreator } from 'zustand';

export interface SensorMode {
    width: number;
    height: number;
    fps: number;
    bitDepth: number;
}

export interface SensorDescriptor {
    id: string;
    vendor: string;
    model: string;
    backend: string;
    modes: SensorMode[];
}

export interface SensorsSlice {
    sensors: {
        list: SensorDescriptor[];
        loading: boolean;
        error: string | null;
        selectedId: string | null;
    };
    setSensors: (list: SensorDescriptor[]) => void;
    setSensorsLoading: (loading: boolean) => void;
    setSensorsError: (error: string | null) => void;
    selectSensor: (id: string | null) => void;
}

export const createSensorsSlice: StateCreator<SensorsSlice, [], [], SensorsSlice> = (set) => ({
    sensors: { list: [], loading: false, error: null, selectedId: null },
    setSensors: (list) =>
        set((s) => ({ sensors: { ...s.sensors, list, loading: false, error: null } })),
    setSensorsLoading: (loading) =>
        set((s) => ({ sensors: { ...s.sensors, loading } })),
    setSensorsError: (error) =>
        set((s) => ({ sensors: { ...s.sensors, error, loading: false } })),
    selectSensor: (id) =>
        set((s) => ({ sensors: { ...s.sensors, selectedId: id } })),
});

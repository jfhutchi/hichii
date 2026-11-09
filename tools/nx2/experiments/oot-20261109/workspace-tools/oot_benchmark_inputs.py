"""Guest input cycles shared by profile training and the matched benchmark."""


def balanced_steps(frames):
    if frames <= 0 or frames % 240:
        raise ValueError("Use complete 240-frame cycles")
    cycle = [
        dict(frames=6, buttons=["X"], left_stick=[0, 0], right_stick=[0, 0]),
        dict(frames=54, buttons=[], left_stick=[0, 0], right_stick=[0, 0]),
        dict(frames=30, buttons=[], left_stick=[0, 1], right_stick=[0, 0]),
        dict(frames=30, buttons=[], left_stick=[0, -1], right_stick=[0, 0]),
        dict(frames=6, buttons=["Y"], left_stick=[0, 0], right_stick=[0, 0]),
        dict(frames=54, buttons=[], left_stick=[0, 0], right_stick=[0, 0]),
        dict(frames=30, buttons=[], left_stick=[0, 0], right_stick=[1, 0]),
        dict(frames=30, buttons=[], left_stick=[0, 0], right_stick=[-1, 0]),
    ]
    return cycle * (frames // 240)

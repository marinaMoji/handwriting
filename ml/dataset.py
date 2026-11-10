import numpy as np
import torch
from torch.utils.data import IterableDataset, get_worker_info

from .augment import augment
from .render import NUM_CHANNELS, fit_to_canvas, render


class TemplateStream(IterableDataset):
    """Endless stream of (9x64x64 uint8 image, class index) from augmented Tegaki templates."""

    def __init__(self, templates, classes, seed=0, size=64, strength=1.0, level=1):
        self.templates = templates
        self.classes = classes
        self.seed = seed
        self.size = size
        self.strength = strength
        self.level = level

    def __iter__(self):
        info = get_worker_info()
        wid = info.id if info else 0
        rng = np.random.default_rng([self.seed, wid])
        n = len(self.classes)
        while True:
            ci = int(rng.integers(n))
            variants = self.templates[self.classes[ci]]
            strokes = variants[int(rng.integers(len(variants)))]
            aug = augment(strokes, rng, self.strength, self.level)
            width = rng.uniform(1.4, 2.4)
            img = render(fit_to_canvas(aug), size=self.size, width=width)
            yield torch.from_numpy(img), ci

"""Compact causal probability model used by the optional training tools."""

from __future__ import annotations

import torch
from torch import nn


class CausalKeyModel(nn.Module):
    """Frame-causal GRU with explicit state and independent key/CW logits.

    Output channel 0 is target-channel key-down probability. Channel 1 is the
    probability that the target channel contains CW rather than an acoustic
    hard negative. It does not emit characters or consume decoded text.
    """

    def __init__(self, feature_count: int = 8, hidden_size: int = 48,
                 layer_count: int = 2) -> None:
        super().__init__()
        self.feature_count = feature_count
        self.hidden_size = hidden_size
        self.layer_count = layer_count
        self.recurrent = nn.GRU(feature_count, hidden_size, layer_count,
                                batch_first=True)
        self.output = nn.Linear(hidden_size, 2)

    def initial_state(self, batch_size: int, *, device=None):
        return torch.zeros(self.layer_count, batch_size, self.hidden_size,
                           device=device)

    def forward(self, features, hidden_in):
        # Features already have fixed physical scaling. Normalizing across the
        # heterogeneous channels of each individual frame erases calibrated
        # level evidence and amplifies weak-frame noise.
        recurrent, hidden_out = self.recurrent(features, hidden_in)
        return self.output(recurrent), hidden_out

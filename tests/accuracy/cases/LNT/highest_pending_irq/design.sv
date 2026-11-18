// Authored for the SVerity accuracy suite. SPDX-License-Identifier: MIT

module top #(
  parameter int unsigned NumIrq    = 12,
  parameter int unsigned GroupSize = 4,
  localparam int unsigned IdxW     = $clog2(NumIrq),
  localparam int unsigned NumGroups = NumIrq / GroupSize
) (
  input  logic                 clk_i,
  input  logic                 rst_ni,
  input  logic [NumIrq-1:0]    pending_i,
  input  logic [NumIrq-1:0]    enable_i,
  output logic                 valid_o,
  output logic [IdxW-1:0]      idx_o,
  output logic [NumGroups-1:0] group_pending_o
);

  logic [NumIrq-1:0] active;
  logic [IdxW-1:0]   idx_d;
  logic              valid_d;
  logic [IdxW-1:0]   idx_q;
  logic              valid_q;

  assign active = pending_i & enable_i;

  always_comb begin
    valid_d = 1'b0;
    idx_d   = '0;
    for (int i = NumIrq - 1; i >= 0; i--) begin
      if (active[i]) begin
        valid_d = 1'b1;
        idx_d   = IdxW'(i);
      end
    end
  end

  always_ff @(posedge clk_i or negedge rst_ni) begin
    if (!rst_ni) begin
      valid_q <= 1'b0;
      idx_q   <= '0;
    end else begin
      valid_q <= valid_d;
      idx_q   <= idx_d;
    end
  end

  always_comb begin
    for (int g = 0; g < NumGroups; g++) begin
      group_pending_o[g] = |active[g*GroupSize +: GroupSize];
    end
  end

  assign valid_o = valid_q;
  assign idx_o   = idx_q;

endmodule

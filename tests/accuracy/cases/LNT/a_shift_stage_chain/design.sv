// Delay chain with a selectable byte from a 24-bit tap word.
module top #(
  parameter int STAGES = 5
) (
  input  logic        clk,
  input  logic        rst_n,
  input  logic        en,
  input  logic [23:0] din,
  input  logic [1:0]  lane,
  output logic [7:0]  byte_o
);
  logic [23:0] stage_q [STAGES];

  always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      for (int i = 0; i < STAGES; i++) stage_q[i] <= 24'h0;
    end else if (en) begin
      stage_q[0] <= din;
      for (int i = 1; i < STAGES; i++) stage_q[i] <= stage_q[i-1];
    end
  end

  always_comb begin
    byte_o = 8'h00;
    case (lane)
      2'd0:    byte_o = stage_q[STAGES-1][7:0];
      2'd1:    byte_o = stage_q[STAGES-1][8 +: 8];
      2'd2:    byte_o = stage_q[STAGES-1][16 +: 8];
      default: byte_o = 8'h00;
    endcase
  end
endmodule

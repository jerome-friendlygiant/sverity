// Authored for the SVerity accuracy suite. SPDX-License-Identifier: MIT
module top #(
  parameter int DEPTH = 16
) (
  input  logic       clk_i,
  input  logic       rst_ni,
  input  logic       push_i,
  input  logic       pop_i,
  output logic       almost_full_o,
  output logic       full_o,
  output logic       empty_o,
  output logic [4:0] level_o
);
  logic [4:0] level_q;
  logic       full;
  logic       empty;
  logic       almost_full;
  logic       do_push;
  logic       do_pop;

  assign full    = (level_q == 5'(DEPTH));
  assign empty   = (level_q == 5'd0);
  assign do_push = push_i & ~full;
  assign do_pop  = pop_i & ~empty;
  assign almost_full = (level_q >= 5'(DEPTH - 2));

  always_ff @(posedge clk_i or negedge rst_ni) begin
    if (!rst_ni) begin
      level_q <= '0;
    end else begin
      case ({do_push, do_pop})
        2'b10:   level_q <= level_q + 5'd1;
        2'b01:   level_q <= level_q - 5'd1;
        default: level_q <= level_q;
      endcase
    end
  end

  assign almost_full_o = almost_full;
  assign full_o        = full;
  assign empty_o       = empty;
  assign level_o       = level_q;
endmodule

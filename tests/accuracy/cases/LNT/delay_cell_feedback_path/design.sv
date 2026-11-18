// Authored for the SVerity accuracy suite. SPDX-License-Identifier: MIT
module top (
  input  logic clk_i,
  input  logic rst_ni,
  input  logic trig_i,
  input  logic load_i,
  input  logic gate_i,
  output logic pulse_o,
  output logic stretched_o
);
  logic fb;
  logic mix;
  logic pulse_q;

  assign mix = load_i ? trig_i : (fb & gate_i);

  trim_delay_cell u_dly (
    .i (mix),
    .z (fb)
  );

  always_ff @(posedge clk_i or negedge rst_ni) begin
    if (!rst_ni) pulse_q <= 1'b0;
    else         pulse_q <= mix & ~pulse_q;
  end

  assign pulse_o     = pulse_q;
  assign stretched_o = mix | fb;
endmodule

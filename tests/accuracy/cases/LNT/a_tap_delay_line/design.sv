// Tapped delay line: new sample enters tap 0 and everything moves one step on enable.
// dout is the oldest tap; fill counts shifts until the line holds valid data.
module top #(
  parameter int TAPS = 6
) (
  input  logic       clk,
  input  logic       rst_n,
  input  logic       en,
  input  logic [7:0] din,
  output logic [7:0] dout,
  output logic       primed
);
  logic [7:0] taps_q [TAPS];
  logic [3:0] fill_q;

  always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      for (int i = 0; i < TAPS; i++) taps_q[i] <= 8'h00;
      fill_q <= 4'd0;
    end else if (en) begin
      taps_q[0] <= din;
      for (int i = 1; i <= TAPS; i++) taps_q[i] <= taps_q[i-1];
      if (fill_q != 4'(TAPS)) fill_q <= fill_q + 4'd1;
    end
  end

  assign dout   = taps_q[TAPS-1];
  assign primed = (fill_q == 4'(TAPS));
endmodule

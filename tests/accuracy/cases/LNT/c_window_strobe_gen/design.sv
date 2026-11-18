// Strobe generator: a window register opens for WIDTH cycles every PERIOD
// cycles and the strobe output carries clock pulses while the window is open.
module top #(
  parameter int PERIOD = 32,
  parameter int WIDTH  = 8
) (
  input  logic clk,
  input  logic rst_n,
  input  logic enable,
  output logic strobe_o,
  output logic window_o
);
  localparam int CW = $clog2(PERIOD);

  logic [CW-1:0] cnt;
  logic          window;

  always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      cnt    <= '0;
      window <= 1'b0;
    end else if (!enable) begin
      cnt    <= '0;
      window <= 1'b0;
    end else begin
      cnt    <= (cnt == CW'(PERIOD - 1)) ? '0 : cnt + 1'b1;
      window <= (cnt < CW'(WIDTH));
    end
  end

  assign strobe_o = clk & window;
  assign window_o = window;
endmodule

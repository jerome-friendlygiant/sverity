// Output channel fed from the primary source; the platform option BYPASS_PATH
// adds the secondary source path.
// The channel register loads only while en is high; y_vld marks the
// cycle after a load.
module top (
  input  logic       clk,
  input  logic       rst_n,
  input  logic       en,
  input  logic [7:0] src_a,
  input  logic [7:0] src_b,
  output logic [7:0] y_q,
  output logic       y_vld
);
  wire [7:0] y;

  assign y = src_a;

`ifdef BYPASS_PATH
  assign y = src_b;
`endif

  always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      y_q   <= 8'h00;
      y_vld <= 1'b0;
    end else begin
      y_vld <= en;
      if (en) y_q <= y;
    end
  end
endmodule

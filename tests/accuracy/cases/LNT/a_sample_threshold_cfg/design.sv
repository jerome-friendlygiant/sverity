`ifdef SIGNED_SENSOR
  `define RAW_T logic signed [7:0]
`else
  `define RAW_T logic [7:0]
`endif
// Threshold detector with a registered result. The sensor format follows
// the platform option SIGNED_SENSOR; the threshold register is signed.
module top (
  input  logic              clk,
  input  logic              rst_n,
  input  `RAW_T             raw,
  input  logic signed [7:0] thr,
  output logic              above_o,
  output logic              fresh_o
);
  logic above_q;
  logic seen_q;

  always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      above_q <= 1'b0;
      seen_q  <= 1'b0;
    end else begin
      above_q <= (raw > thr);
      seen_q  <= 1'b1;
    end
  end

  assign above_o = above_q;
  assign fresh_o = seen_q;
endmodule

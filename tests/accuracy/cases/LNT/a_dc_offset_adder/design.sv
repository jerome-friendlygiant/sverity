// Adds an unsigned calibration offset to a signed 8-bit sample.
module top (
  input  logic              clk,
  input  logic              rst_n,
  input  logic              in_valid,
  input  logic signed [7:0] sample_s,
  input  logic [7:0]        offset_u,
  output logic              out_valid,
  output logic signed [15:0] biased
);
  logic signed [15:0] biased_q;
  logic               valid_q;

  always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      biased_q <= 16'sd0;
      valid_q  <= 1'b0;
    end else begin
      valid_q <= in_valid;
      if (in_valid) biased_q <= sample_s + offset_u;
    end
  end

  assign biased    = biased_q;
  assign out_valid = valid_q;
endmodule

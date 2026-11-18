// Applies a 4-bit gain to a 12-bit unsigned sample; one-cycle latency.
module top (
  input  logic        clk,
  input  logic        rst_n,
  input  logic        in_valid,
  input  logic [11:0] sample,
  input  logic [3:0]  gain,
  output logic        out_valid,
  output logic [11:0] scaled
);
  logic        valid_q;
  logic [11:0] scaled_q;

  always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      valid_q  <= 1'b0;
      scaled_q <= 12'h000;
    end else begin
      valid_q <= in_valid;
      if (in_valid) scaled_q <= sample * gain;
    end
  end

  assign out_valid = valid_q;
  assign scaled    = scaled_q;
endmodule

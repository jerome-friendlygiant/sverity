// Two-stage sample delay with a dither mask; the valid flag follows in parallel.
module top (
  input  logic        clk,
  input  logic        rst_n,
  input  logic [15:0] sample_in,
  input  logic        valid_in,
  input  logic [15:0] dither_mask,
  output logic [15:0] sample_out,
  output logic        valid_out
);
  logic [15:0] stage1;
  logic [15:0] stage2;
  logic        v1;

  always_ff @(posedge clk) begin
    stage1 = sample_in ^ dither_mask;
    stage2 = stage1;
  end

  assign sample_out = stage2;

  always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      v1        <= 1'b0;
      valid_out <= 1'b0;
    end else begin
      v1        <= valid_in;
      valid_out <= v1;
    end
  end
endmodule

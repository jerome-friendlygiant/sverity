// Horizontal 2:1 downscale: averages each pair of consecutive 8-bit pixels.
module top (
  input  logic       clk,
  input  logic       rst_n,
  input  logic       pix_valid,
  input  logic [7:0] pix,
  output logic       avg_valid,
  output logic [7:0] avg
);
  logic [7:0] prev_q;
  logic [7:0] avg_q;
  logic       have_prev_q;
  logic       avg_valid_q;

  always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      prev_q      <= 8'h00;
      avg_q       <= 8'h00;
      have_prev_q <= 1'b0;
      avg_valid_q <= 1'b0;
    end else begin
      avg_valid_q <= 1'b0;
      if (pix_valid) begin
        if (have_prev_q) begin
          avg_q       <= (prev_q + pix) >> 1;
          avg_valid_q <= 1'b1;
          have_prev_q <= 1'b0;
        end else begin
          prev_q      <= pix;
          have_prev_q <= 1'b1;
        end
      end
    end
  end

  assign avg       = avg_q;
  assign avg_valid = avg_valid_q;
endmodule

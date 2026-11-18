// Block averager: sums TAPS samples, discards the SKIP warm-up samples from
// the divisor and reports the mean of the remaining ones at the end of a block.
module top (
  input  logic       clk,
  input  logic       rst_n,
  input  logic       sample_vld,
  input  logic [7:0] sample,
  output logic [7:0] avg_o,
  output logic       avg_vld
);
  localparam int TAPS = 4;
  localparam int SKIP = 4;
  localparam int DIVISOR = TAPS - SKIP;

  logic [15:0] acc;
  logic [2:0]  cnt;

  always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      acc     <= 16'd0;
      cnt     <= 3'd0;
      avg_o   <= 8'd0;
      avg_vld <= 1'b0;
    end else begin
      avg_vld <= 1'b0;
      if (sample_vld) begin
        if (cnt == 3'(TAPS - 1)) begin
          avg_o   <= 8'((acc + 16'(sample)) / 16'(DIVISOR));
          avg_vld <= 1'b1;
          acc     <= 16'd0;
          cnt     <= 3'd0;
        end else begin
          acc <= acc + 16'(sample);
          cnt <= cnt + 3'd1;
        end
      end
    end
  end
endmodule

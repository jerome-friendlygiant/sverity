// Moving-window mean over TAPS samples after WARMUP samples are dropped.
// Both lengths are parameters of the block.
module top #(
  parameter int TAPS   = 16,
  parameter int WARMUP = 4
) (
  input  logic        clk,
  input  logic        rst_n,
  input  logic        vld_i,
  input  logic [7:0]  sample_i,
  output logic [7:0]  mean_o,
  output logic        mean_vld
);
  localparam int DIV = TAPS - WARMUP;
  localparam int CW  = $clog2(TAPS) + 1;

  logic [23:0]   acc;
  logic [CW-1:0] cnt;

  always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      acc      <= 24'd0;
      cnt      <= '0;
      mean_o   <= 8'd0;
      mean_vld <= 1'b0;
    end else begin
      mean_vld <= 1'b0;
      if (vld_i) begin
        if (cnt == CW'(TAPS - 1)) begin
          mean_o   <= 8'((acc + 24'(sample_i)) / 24'(DIV));
          mean_vld <= 1'b1;
          acc      <= 24'd0;
          cnt      <= '0;
        end else begin
          acc <= acc + 24'(sample_i);
          cnt <= cnt + 1'b1;
        end
      end
    end
  end
endmodule

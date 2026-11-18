// Oversampling bit counter. A divider produces a one-cycle enable and the
// bit counter advances on that enable; every register uses the same clock.
module top #(
  parameter int DIV = 12
) (
  input  logic       clk,
  input  logic       rst_n,
  input  logic       run,
  output logic [3:0] bit_cnt,
  output logic       frame_end
);
  localparam int CW = $clog2(DIV);

  logic [CW-1:0] div_cnt;
  logic          tick;

  always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      div_cnt <= '0;
      tick    <= 1'b0;
    end else if (!run) begin
      div_cnt <= '0;
      tick    <= 1'b0;
    end else if (div_cnt == CW'(DIV - 1)) begin
      div_cnt <= '0;
      tick    <= 1'b1;
    end else begin
      div_cnt <= div_cnt + 1'b1;
      tick    <= 1'b0;
    end
  end

  always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n)                  bit_cnt <= 4'd0;
    else if (!run)               bit_cnt <= 4'd0;
    else if (tick)               bit_cnt <= (bit_cnt == 4'd9) ? 4'd0 : bit_cnt + 4'd1;
  end

  assign frame_end = tick && (bit_cnt == 4'd9);
endmodule

// Receive error monitor: counts framing and parity errors and reports a
// sticky error indication until the controller clears the counters.
module top (
  input  logic       clk,
  input  logic       rst_n,
  input  logic       frame_err_i,
  input  logic       parity_err_i,
  input  logic       clear_i,
  output logic [7:0] err_cnt_o,
  output logic       err_o
);
  logic [7:0] err_cnt;
  logic       err_sticky;

  always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n)       err_cnt <= 8'd0;
    else if (clear_i) err_cnt <= 8'd0;
    else if ((frame_err_i || parity_err_i) && err_cnt != 8'hff)
      err_cnt <= err_cnt + 8'd1;
  end

  always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n) err_sticky <= 1'b0;
  end

  assign err_cnt_o = err_cnt;
  assign err_o     = err_sticky | (err_cnt != 8'd0);
endmodule

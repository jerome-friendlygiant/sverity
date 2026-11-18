// Retry back-off timer. A failure starts a wait of cfg_timeout cycles and
// retry_o pulses when the wait expires. The timeout is software programmable.
module top (
  input  logic       clk,
  input  logic       rst_n,
  input  logic       cfg_we,
  input  logic [7:0] cfg_wdata,
  input  logic       fail_i,
  output logic       retry_o
);
  logic [7:0] cfg_timeout;
  logic [7:0] wait_cnt;
  logic       waiting;

  always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n)      cfg_timeout <= 8'd32;
    else if (cfg_we) cfg_timeout <= cfg_wdata;
  end

  always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      wait_cnt <= cfg_timeout;
      waiting  <= 1'b0;
    end else if (fail_i && !waiting) begin
      wait_cnt <= cfg_timeout;
      waiting  <= 1'b1;
    end else if (waiting) begin
      if (wait_cnt == 8'd0) waiting <= 1'b0;
      else                  wait_cnt <= wait_cnt - 8'd1;
    end
  end

  assign retry_o = waiting && (wait_cnt == 8'd0);
endmodule

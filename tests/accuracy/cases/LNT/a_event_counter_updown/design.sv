// Outstanding-event counter: incremented by issue pulses, decremented by completion pulses.
// The counter saturates at 255 and at 0; idle is high when nothing is outstanding.
// The issue and completion paths are kept in separate processes.
module top (
  input  logic       clk,
  input  logic       rst_n,
  input  logic       inc,
  input  logic       dec,
  output logic [7:0] count,
  output logic       idle,
  output logic       full
);
  logic [7:0] cnt_q;

  always @(posedge clk or negedge rst_n) begin
    if (!rst_n)                           cnt_q <= 8'd0;
    else if (inc && cnt_q != 8'hFF)       cnt_q <= cnt_q + 8'd1;
  end

  always @(posedge clk or negedge rst_n) begin
    if (!rst_n)                           cnt_q <= 8'd0;
    else if (dec && cnt_q != 8'd0)        cnt_q <= cnt_q - 8'd1;
  end

  assign count = cnt_q;
  assign idle  = (cnt_q == 8'd0);
  assign full  = (cnt_q == 8'hFF);
endmodule

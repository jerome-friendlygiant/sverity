module top(input logic clk,
  advance,
  output logic [7:0] work);

  logic [2:0] mode = 3'd1;
  wire work_reset_n;
  always_ff @(posedge clk) if (advance) mode <= mode + 1'b1;
  assign work_reset_n = (mode != 3'd0);
  always_ff @(posedge clk or negedge work_reset_n) begin
    if (!work_reset_n) work <= '0;
    else work <= work + 1'b1;
  end

endmodule

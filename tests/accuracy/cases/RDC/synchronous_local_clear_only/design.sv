module top(input logic clk,
  clear_command,
  output logic [7:0] count);

  logic clear_q = 1;
  always_ff @(posedge clk) begin
    clear_q <= clear_command;
  end
  always_ff @(posedge clk) begin
    if (clear_q) count <= '0;
    else count <= count + 1'b1;
  end

endmodule

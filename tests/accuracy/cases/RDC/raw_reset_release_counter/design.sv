module top(input logic clk,
  rst_n,
  enable,
  output logic [3:0] count);

  // rst_n may rise at any point in the core cycle.
  always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n) count <= '0;
    else if (enable) count <= count + 1'b1;
  end

endmodule

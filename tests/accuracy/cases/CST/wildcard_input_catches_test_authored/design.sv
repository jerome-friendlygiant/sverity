module testable_counter #(parameter int WIDTH = 8) (
  input logic core_clk,
  input logic reset_i,
  input logic test_i,
  input logic [WIDTH-1:0] payload_i,
  output logic [WIDTH-1:0] count_o
);
  logic [WIDTH-1:0] count_q;
  always_ff @(posedge core_clk or posedge reset_i) begin
    if (reset_i)
      count_q <= '0;
    else if (test_i)
      count_q <= '0;
    else
      count_q <= count_q + payload_i;
  end
  assign count_o = count_q;
endmodule

module top (
  input logic core_clk,
  input logic reset_i,
  input logic test_i,
  input logic [7:0] payload_i,
  output logic [7:0] count_o
);
  testable_counter #(.WIDTH(8)) u_counter (.*);
endmodule

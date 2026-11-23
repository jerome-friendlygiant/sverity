module case_ib03(input logic clk, input logic async_pin, output logic q);
 always_ff @(posedge clk) q<=async_pin;
endmodule

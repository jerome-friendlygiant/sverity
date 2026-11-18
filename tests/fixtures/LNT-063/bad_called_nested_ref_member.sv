// expect: LNT-063 WARNING 6
module top(input logic clk, d, output logic q);
  struct { logic value; logic spare; } state;
  task automatic leaf(ref logic target); target = d; endtask
  task automatic forward_write(ref logic target); leaf(target); endtask
  always @(posedge clk) begin state.value <= d; forward_write(state.value); end
  assign q = state.value;
endmodule

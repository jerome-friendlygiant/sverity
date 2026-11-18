// sverity-args: --top top
// expect: LNT-060 WARNING
module top(input logic clk, output logic y);
  logic q;
  task automatic put; q = 1; endtask
  function automatic logic get(); return q; endfunction
  task automatic copy; y = get(); endtask
  always @(posedge clk) put();
  always_comb copy();
endmodule

// sverity-args: --top top
// A continue makes the path analysis approximate, so assignment of the output on every path is not proven.
// expect: LNT-013 WARNING 5
module top(input logic [3:0] a, output logic [3:0] q);
  task automatic scan(input logic [3:0] v, output logic [3:0] o);
    o = '0;
    for (int i = 0; i < 4; i++) begin
      if (!v[i]) continue;
      o[i] = 1'b1;
    end
  endtask
  always_comb scan(a, q);
endmodule

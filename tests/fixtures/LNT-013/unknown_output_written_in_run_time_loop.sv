// sverity-args: --top top
// The run-time loop writes `o`: whether it runs at all is not known, so `o` unassigned on some path cannot be called certain.
// expect: LNT-013 WARNING 6
// expect-unresolved: LNT-013 6
module top(input logic [7:0] x, input logic [3:0] n, output logic [7:0] y);
  task automatic t(input logic [7:0] v, input logic [3:0] k, output logic [7:0] o);
    for (logic [3:0] i = 0; i < k; i++) o = v;
  endtask
  always_comb t(x, n, y);
endmodule

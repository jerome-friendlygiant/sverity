// sverity-args: --top top
// A default before the condition, and a return on both branches: every path assigns the output and the result.
// expect-none: LNT-013
module top(input logic [7:0] a, input logic bypass, output logic [7:0] q, output logic [7:0] r);
  task automatic fold(input logic [7:0] v, input logic skip, output logic [7:0] o);
    o = v;
    if (!skip) o = v ^ 8'h5a;
  endtask
  function automatic logic [7:0] pick(input logic [7:0] v, input logic s);
    if (s) return v;
    else return ~v;
  endfunction
  always_comb fold(a, bypass, q);
  assign r = pick(a, bypass);
endmodule

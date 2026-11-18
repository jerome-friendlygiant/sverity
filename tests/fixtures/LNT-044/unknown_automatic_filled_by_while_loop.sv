// sverity-args: --top top
// A while loop is walked once, so which bits of the automatic local it fills is not known: Unresolved, not a proven read.
// expect: LNT-044 WARNING 10
module top(input logic [3:0] a, output logic [3:0] y);
  function automatic logic [3:0] f(input logic [3:0] v);
    logic [3:0] s;
    int i;
    i = 0;
    while (i < 4) begin s[i] = v[i]; i++; end
    return s;
  endfunction
  assign y = f(a);
endmodule

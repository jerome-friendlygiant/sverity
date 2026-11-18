// sverity-args: --top top
// The foreach leaves element 3 unwritten, and the return reads the whole array.
// expect: LNT-044 WARNING 8
module top(input logic [7:0] a, output logic [7:0] y);
  function automatic logic [7:0] pick(input logic [7:0] v);
    logic [7:0] r;
    foreach (r[k]) if (k != 3) r[k] = v[k];
    return r;
  endfunction
  assign y = pick(a);
endmodule

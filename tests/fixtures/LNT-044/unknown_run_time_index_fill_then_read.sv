// sverity-args: --top top
// The loop writes o[pm[k]]: whether the run-time indices together cover every bit is not decidable, so the read is Unresolved.
// expect: LNT-044 WARNING 8
module top(input logic [3:0] a, input logic [3:0][1:0] perm, output logic [3:0] y);
  function automatic logic [3:0] p(logic [3:0] s, logic [3:0][1:0] pm);
    logic [3:0] o;
    for (int k = 0; k < 4; k++) o[pm[k]] = s[k];
    return o;
  endfunction
  assign y = p(a, perm);
endmodule

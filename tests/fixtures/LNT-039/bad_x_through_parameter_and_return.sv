// sverity-args: --top top
// X/Z reaching an assignment through a localparam or a function return is reported like a direct literal.
// expect: LNT-039 WARNING
module top(input logic sel, input logic [3:0] d, output logic [3:0] q, r);
  localparam logic [3:0] UNK = 4'bxxxx;
  function automatic logic [3:0] get();
    return 4'bxxxx;
  endfunction
  assign q = sel ? d : UNK;
  assign r = get();
endmodule

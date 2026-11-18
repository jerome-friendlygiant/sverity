// sverity-args: --top top
// A casex in a function body is user code: the statement is reported where it is written.
// expect: LNT-016 WARNING 6
module top(input logic [1:0] s, output logic [1:0] y);
  function automatic logic [1:0] pick(input logic [1:0] v);
    casex (v)
      2'b1x: pick = 2'd1;
      default: pick = 2'd2;
    endcase
  endfunction
  assign y = pick(s);
endmodule

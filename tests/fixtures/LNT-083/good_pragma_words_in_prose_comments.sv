// sverity-args: --top top
// A comment that merely mentions a tool and a directive is no pragma: the directive must follow the tool word directly.
// expect-none: LNT-083
module top(input logic a, b, output logic y);
  // synthesis on an FPGA maps this to a LUT
  // Synopsys DC needs no translate_off here
  assign y = a & b;
  // pragma: nothing is switched off, translate_off is only a word in this sentence
endmodule

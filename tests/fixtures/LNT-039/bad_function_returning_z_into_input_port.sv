// sverity-args: --top top
// A function that returns 'z hands the input the same floating value a literal would.
// expect: LNT-039 WARNING 9
module sub(input wire a, output wire y);
  assign y = a;
endmodule
module top(output wire y);
  function automatic logic hiz(); return 1'bz; endfunction
  sub u(.a(hiz()), .y(y));
endmodule

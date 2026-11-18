// sverity-args: --top top
// The region holds an instance whose output drives a design signal: synthesis does not build the instance.
// expect: LNT-083 WARNING 9
module drv(output logic o);
  assign o = 1'b1;
endmodule
module top(input logic a, output logic y, output logic w);
  assign y = a;
  // synthesis translate_off
  drv u (.o(w));
  // synthesis translate_on
endmodule

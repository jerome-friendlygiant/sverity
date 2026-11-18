// sverity-args: --top top
// X/Z bits of an inside set element, literal or named constant, are wildcards (IEEE 1800-2017 11.4.13): not LNT-040.
// expect-none: LNT-040
module top(input logic [1:0] a, output logic y, z);
  localparam logic [1:0] DC = 2'bz1;
  assign y = a inside {2'b00, DC};
  assign z = a inside {2'b1x};
endmodule

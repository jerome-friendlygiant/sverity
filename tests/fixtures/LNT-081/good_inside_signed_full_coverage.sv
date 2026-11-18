// sverity-args: --top top
// expect-none: LNT-081
// Every bound and the 2-state selector are signed, so both readings type the set signed and [-8:7] covers every value.
module top(input bit signed [3:0] s, output logic y);
  always_comb begin
    y = 1'b0;
    priority case (s) inside
      [-8:-1]: y = 1'b1;
      [0:7]: y = 1'b0;
    endcase
  end
endmodule

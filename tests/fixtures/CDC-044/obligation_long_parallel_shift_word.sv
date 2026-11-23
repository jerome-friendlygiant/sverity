// sverity-args: --top top
// expect: CDC-044 WARNING
module top(input logic clka, rx, output logic [17:0] w);
logic s0,s1,s2,s3,s4,s5,s6,s7,s8,s9,s10,s11,s12,s13,s14,s15,s16,s17;
always_ff @(posedge clka) begin s0<=rx; s1<=s0; s2<=s1; s3<=s2; s4<=s3; s5<=s4; s6<=s5; s7<=s6; s8<=s7; s9<=s8; s10<=s9; s11<=s10; s12<=s11; s13<=s12; s14<=s13; s15<=s14; s16<=s15; s17<=s16; end
assign w = {s17,s16,s15,s14,s13,s12,s11,s10,s9,s8,s7,s6,s5,s4,s3,s2,s1,s0};
endmodule

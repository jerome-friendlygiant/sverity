// sverity-args: --top top
// Grouped specialisations: only the all-wildcard parameter shadows the next arm.
// expect-count: LNT-064 WARNING 2
// expect: LNT-064 WARNING 9
module decoder #(parameter logic [1:0] PATTERN=2'b??)(input logic [1:0] s, output logic y);
 always_comb begin
  y=0;
  casez(s)
   PATTERN: y=1;
   2'b01: y=0;
  endcase
 end
endmodule
module top(input logic [1:0] s, output logic [2:0] y);
 decoder a(s,y[0]); decoder b(s,y[1]); decoder #(.PATTERN(2'b10)) c(s,y[2]);
endmodule

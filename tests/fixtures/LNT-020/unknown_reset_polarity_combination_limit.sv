// sverity-args: --top top
// expect: LNT-020 WARNING
// expect-unresolved: LNT-020
module top(input logic clk,d,c0,c1,c2,c3,c4,c5,c6, output logic s,y);
logic h0,l0,z0; always_ff @(posedge clk or posedge c0) if(c0) h0<=0; else h0<=d; always_ff @(posedge clk or negedge c0) if(!c0) l0<=0; else l0<=d;
logic h1,l1,z1; always_ff @(posedge clk or posedge c1) if(c1) h1<=0; else h1<=d; always_ff @(posedge clk or negedge c1) if(!c1) l1<=0; else l1<=d;
logic h2,l2,z2; always_ff @(posedge clk or posedge c2) if(c2) h2<=0; else h2<=d; always_ff @(posedge clk or negedge c2) if(!c2) l2<=0; else l2<=d;
logic h3,l3,z3; always_ff @(posedge clk or posedge c3) if(c3) h3<=0; else h3<=d; always_ff @(posedge clk or negedge c3) if(!c3) l3<=0; else l3<=d;
logic h4,l4,z4; always_ff @(posedge clk or posedge c4) if(c4) h4<=0; else h4<=d; always_ff @(posedge clk or negedge c4) if(!c4) l4<=0; else l4<=d;
logic h5,l5,z5; always_ff @(posedge clk or posedge c5) if(c5) h5<=0; else h5<=d; always_ff @(posedge clk or negedge c5) if(!c5) l5<=0; else l5<=d;
logic h6,l6,z6; always_ff @(posedge clk or posedge c6) if(c6) h6<=0; else h6<=d; always_ff @(posedge clk or negedge c6) if(!c6) l6<=0; else l6<=d;
always_ff @(posedge clk) begin
if(c0) s<=0; else z0<=d;
if(c1) s<=0; else z1<=d;
if(c2) s<=0; else z2<=d;
if(c3) s<=0; else z3<=d;
if(c4) s<=0; else z4<=d;
if(c5) s<=0; else z5<=d;
if(c6) s<=0; else z6<=d;
end
assign y=s; endmodule

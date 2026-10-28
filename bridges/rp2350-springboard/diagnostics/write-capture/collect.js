// Download-only. Invoked by the established serial palm-sync run command.
const fs=require('fs'),path=require('path');
const ps=require(path.resolve(__dirname,'../../../../../fujinet-palm/tools/palm-sync'));
exports.run=async conn=>{
 const out=process.env.DF_RESULT_DIR;if(!out)throw new Error('DF_RESULT_DIR required');
 fs.mkdirSync(out,{recursive:true});
 const result={complete:false,databases:[]};
 for(const [name,file] of [['FN Write Capture E3','results.pdb'],['CS1 Write E3','app.prc']]){
  const db=await ps.readRawDb(conn,name);const bytes=db.serialize();fs.writeFileSync(path.join(out,file),bytes);result.databases.push({name,file,bytes:bytes.length});
 }
 result.complete=true;fs.writeFileSync(path.join(out,'download.json'),JSON.stringify(result,null,2));
};
